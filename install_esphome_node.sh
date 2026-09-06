#!/usr/bin/env bash
set -euo pipefail

# Must be root before doing anything else.
if [[ $EUID -ne 0 ]]; then
    echo "ERROR: This script must be run as root." >&2
    exit 1
fi

# Naming/path settings may be overridden using environment variables.
INSTALL_DIR="${INSTALL_DIR:-/opt/esphome-node}"
INSTALL_PATH="${INSTALL_PATH:-${INSTALL_DIR}/esphome-node}"
SERVICE_NAME="${SERVICE_NAME:-esphome-node}"
SERVICE_USER="${SERVICE_USER:-esphome}"
SERVICE_GROUP="${SERVICE_GROUP:-esphome}"

SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

# Policy settings may be supplied by environment or command line.
OTA="${OTA:-}"
SYSTEM_WRITE="${SYSTEM_WRITE:-}"
HOME_ACCESS="${HOME_ACCESS:-}"
ALLOW_PRIVILEGE_GAIN="${ALLOW_PRIVILEGE_GAIN:-}"

usage() {
    cat <<EOF
Usage:
  $0 [options] <path-to-esphome-binary>

Options:
  --ota=true|false
      Allow ESPHome OTA to replace/update the installed executable.
      Default: true

  --system-write=true|false
      Allow WRITE access to system filesystem paths.

      false (default):
          System paths are generally readable, but read-only.

      true:
          systemd does not make system paths read-only.
          Normal Unix permissions still apply.

      Default: false

  --home-access=true|false
      Allow access to /home, /root and /run/user.

      false (default):
          These paths are inaccessible for both reading and writing.

      true:
          systemd does not restrict access. Normal Unix permissions
          determine what can be read or written.

      Default: false

  --allow-privilege-gain=true|false
      Allow the service and commands it starts to gain additional
      privileges, for example through sudo or setuid programs.

      false (default):
          NoNewPrivileges=true is enabled.

      true:
          NoNewPrivileges is disabled. This may be required if
          host_command runs sudo or similar tools.

      This does not itself grant sudo/root access. sudoers and normal
      system permissions must still permit the requested operation.

      Default: false

  -h, --help
      Show this help.

Environment variables:

  INSTALL_DIR              Default: /opt/esphome-node
  INSTALL_PATH             Default: \$INSTALL_DIR/esphome-node
  SERVICE_NAME             Default: esphome-node
  SERVICE_USER             Default: esphome
  SERVICE_GROUP            Default: esphome

  OTA                      Default: true
  SYSTEM_WRITE             Default: false
  HOME_ACCESS              Default: false
  ALLOW_PRIVILEGE_GAIN     Default: false

Policy options may be set using either environment variables or command-line
options. Command-line options take precedence over environment variables.

If a policy option is unset and stdin is interactive, you will be prompted.
If it is unset in a non-interactive run, the default shown above is used.
EOF
}

parse_bool() {
    local value="${1,,}"

    case "${value}" in
        true|false)
            printf '%s' "${value}"
            ;;
        *)
            echo "ERROR: Expected true or false, got: ${1}" >&2
            exit 2
            ;;
    esac
}

prompt_bool() {
    local prompt="$1"
    local default="$2"
    local answer

    while true; do
        if [[ "${default}" == "true" ]]; then
            read -r -p "${prompt} [Y/n]: " answer
            answer="${answer:-y}"
        else
            read -r -p "${prompt} [y/N]: " answer
            answer="${answer:-n}"
        fi

        case "${answer,,}" in
            y|yes|true)
                printf 'true'
                return
                ;;
            n|no|false)
                printf 'false'
                return
                ;;
            *)
                echo "Please answer yes or no." >&2
                ;;
        esac
    done
}

# Validate policy values supplied through environment variables.
if [[ -n "${OTA}" ]]; then
    OTA="$(parse_bool "${OTA}")"
fi

if [[ -n "${SYSTEM_WRITE}" ]]; then
    SYSTEM_WRITE="$(parse_bool "${SYSTEM_WRITE}")"
fi

if [[ -n "${HOME_ACCESS}" ]]; then
    HOME_ACCESS="$(parse_bool "${HOME_ACCESS}")"
fi

if [[ -n "${ALLOW_PRIVILEGE_GAIN}" ]]; then
    ALLOW_PRIVILEGE_GAIN="$(parse_bool "${ALLOW_PRIVILEGE_GAIN}")"
fi

SOURCE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ota=*)
            OTA="$(parse_bool "${1#*=}")"
            shift
            ;;

        --system-write=*)
            SYSTEM_WRITE="$(parse_bool "${1#*=}")"
            shift
            ;;

        --home-access=*)
            HOME_ACCESS="$(parse_bool "${1#*=}")"
            shift
            ;;

        --allow-privilege-gain=*)
            ALLOW_PRIVILEGE_GAIN="$(parse_bool "${1#*=}")"
            shift
            ;;

        -h|--help)
            usage
            exit 0
            ;;

        -*)
            echo "ERROR: Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;

        *)
            if [[ -n "${SOURCE}" ]]; then
                echo "ERROR: More than one source binary specified." >&2
                exit 2
            fi

            SOURCE="$1"
            shift
            ;;
    esac
done

if [[ -z "${SOURCE}" ]]; then
    echo "ERROR: No source binary specified." >&2
    usage >&2
    exit 2
fi

if [[ ! -f "${SOURCE}" ]]; then
    echo "ERROR: Source file does not exist: ${SOURCE}" >&2
    exit 2
fi

# Prompt for policy settings not supplied through CLI or environment.
if [[ -t 0 ]]; then
    if [[ -z "${OTA}" ]]; then
        OTA="$(prompt_bool \
            "Allow ESPHome OTA to replace/update ${INSTALL_PATH}?" \
            true)"
    fi

    if [[ -z "${SYSTEM_WRITE}" ]]; then
        SYSTEM_WRITE="$(prompt_bool \
            "Allow WRITE access to system paths? (No = read-only)" \
            false)"
    fi

    if [[ -z "${HOME_ACCESS}" ]]; then
        HOME_ACCESS="$(prompt_bool \
            "Allow access to /home, /root and /run/user? (No = inaccessible)" \
            false)"
    fi

    if [[ -z "${ALLOW_PRIVILEGE_GAIN}" ]]; then
        ALLOW_PRIVILEGE_GAIN="$(prompt_bool \
            "Allow commands to gain privileges, e.g. using sudo?" \
            false)"
    fi
else
    OTA="${OTA:-true}"
    SYSTEM_WRITE="${SYSTEM_WRITE:-false}"
    HOME_ACCESS="${HOME_ACCESS:-false}"
    ALLOW_PRIVILEGE_GAIN="${ALLOW_PRIVILEGE_GAIN:-false}"
fi

echo
echo "Configuration:"
echo "  Source binary:        ${SOURCE}"
echo "  Install directory:    ${INSTALL_DIR}"
echo "  Install path:         ${INSTALL_PATH}"
echo "  Service name:         ${SERVICE_NAME}"
echo "  Service user:         ${SERVICE_USER}"
echo "  Service group:        ${SERVICE_GROUP}"
echo "  OTA updates:          ${OTA}"
echo "  System WRITE access:  ${SYSTEM_WRITE}"
echo "  Home access:          ${HOME_ACCESS}"
echo "  Privilege gain:       ${ALLOW_PRIVILEGE_GAIN}"
echo

changed_binary=false
changed_service=false

# Create dedicated system group/user if necessary.
if ! getent group "${SERVICE_GROUP}" >/dev/null; then
    groupadd --system "${SERVICE_GROUP}"
fi

if ! id "${SERVICE_USER}" >/dev/null 2>&1; then
    useradd \
        --system \
        --gid "${SERVICE_GROUP}" \
        --home-dir "${INSTALL_DIR}" \
        --no-create-home \
        --shell /usr/sbin/nologin \
        "${SERVICE_USER}"
fi

# OTA requires the service account to be able to replace its executable.
if [[ "${OTA}" == "true" ]]; then
    INSTALL_MODE="0770"
else
    INSTALL_MODE="0750"
fi

install \
    --directory \
    --owner=root \
    --group="${SERVICE_GROUP}" \
    --mode="${INSTALL_MODE}" \
    "${INSTALL_DIR}"

# Install/update the executable only when its contents differ.
if [[ ! -f "${INSTALL_PATH}" ]] || ! cmp -s "${SOURCE}" "${INSTALL_PATH}"; then
    echo "Installing ${SOURCE} -> ${INSTALL_PATH}"

    INSTALL_TMP="${INSTALL_PATH}.new.$$"

    install \
        --owner=root \
        --group="${SERVICE_GROUP}" \
        --mode="${INSTALL_MODE}" \
        "${SOURCE}" \
        "${INSTALL_TMP}"

    mv -f "${INSTALL_TMP}" "${INSTALL_PATH}"

    changed_binary=true
else
    chown root:"${SERVICE_GROUP}" "${INSTALL_PATH}"
    chmod "${INSTALL_MODE}" "${INSTALL_PATH}"
fi

SERVICE_TMP="$(mktemp)"
trap 'rm -f "${SERVICE_TMP}" "${INSTALL_TMP:-}"' EXIT

cat >"${SERVICE_TMP}" <<EOF
[Unit]
Description=ESPHome node
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${SERVICE_USER}
Group=${SERVICE_GROUP}
WorkingDirectory=${INSTALL_DIR}
ExecStart=${INSTALL_PATH}

Restart=on-failure
RestartSec=5

# Prevent privilege escalation by this service and its child processes.
NoNewPrivileges=$([[ "${ALLOW_PRIVILEGE_GAIN}" == "true" ]] && echo "false" || echo "true")

# Give the service private /tmp and /var/tmp namespaces.
PrivateTmp=true

EOF

if [[ "${SYSTEM_WRITE}" == "false" ]]; then
    cat >>"${SERVICE_TMP}" <<EOF
# Make system filesystem paths read-only to the service.
ProtectSystem=strict

EOF

    if [[ "${OTA}" == "true" ]]; then
        cat >>"${SERVICE_TMP}" <<EOF
# Keep the installation directory writable for ESPHome OTA.
ReadWritePaths=${INSTALL_DIR}

EOF
    fi
else
    cat >>"${SERVICE_TMP}" <<EOF
# Allow writes to system paths subject to normal Unix permissions.
ProtectSystem=false

EOF
fi

if [[ "${HOME_ACCESS}" == "false" ]]; then
    cat >>"${SERVICE_TMP}" <<EOF
# Make /home, /root and /run/user inaccessible to the service.
ProtectHome=true

EOF
else
    cat >>"${SERVICE_TMP}" <<EOF
# Allow home-directory access subject to normal Unix permissions.
ProtectHome=false

EOF
fi

cat >>"${SERVICE_TMP}" <<EOF
[Install]
WantedBy=multi-user.target
EOF

# Install/update the service file only when its contents differ.
if [[ ! -f "${SERVICE_FILE}" ]] || ! cmp -s "${SERVICE_TMP}" "${SERVICE_FILE}"; then
    echo "Installing systemd service: ${SERVICE_FILE}"

    install \
        --owner=root \
        --group=root \
        --mode=0644 \
        "${SERVICE_TMP}" \
        "${SERVICE_FILE}"

    changed_service=true
fi

if [[ "${changed_service}" == "true" ]]; then
    systemctl daemon-reload
fi

systemctl enable "${SERVICE_NAME}.service" >/dev/null

# Restart only if the binary or service configuration changed.
if systemctl is-active --quiet "${SERVICE_NAME}.service"; then
    if [[ "${changed_binary}" == "true" || "${changed_service}" == "true" ]]; then
        echo "Restarting ${SERVICE_NAME}.service"
        systemctl restart "${SERVICE_NAME}.service"
    else
        echo "${SERVICE_NAME}.service is already up to date and running"
    fi
else
    echo "Starting ${SERVICE_NAME}.service"
    systemctl start "${SERVICE_NAME}.service"
fi

echo
systemctl --no-pager --full status "${SERVICE_NAME}.service" || true