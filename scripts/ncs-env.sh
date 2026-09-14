#!/usr/bin/env bash
# Activate nRF Connect SDK toolchain + west (Codespaces overrides image ENTRYPOINT).
# shellcheck shell=bash

ncs_activate() {
	export NRFUTIL_HOME="${NRFUTIL_HOME:-/usr/local/share/nrfutil}"
	export NCS_VERSION="${NCS_VERSION:-v2.9.0}"

	if [[ -z "${NCS_DIR:-}" ]]; then
		if [[ -d /workdir/zephyr ]] || [[ -d /workdir/.west ]]; then
			NCS_DIR=/workdir
		elif [[ -d "${HOME}/ncs/zephyr" ]]; then
			NCS_DIR="${HOME}/ncs"
		else
			NCS_DIR=/workdir
		fi
	fi
	export NCS_DIR

	if command -v nrfutil >/dev/null 2>&1; then
		local env_script
		env_script="$(mktemp)"
		if nrfutil toolchain-manager env --as-script --ncs-version "${NCS_VERSION}" >"${env_script}" 2>/dev/null \
			|| nrfutil toolchain-manager env --as-script >"${env_script}" 2>/dev/null; then
			# shellcheck disable=SC1090
			local nounset_enabled=0
			case "$-" in
				*u*) nounset_enabled=1; set +u ;;
			esac
			source "${env_script}"
			if [[ ${nounset_enabled} -eq 1 ]]; then
				set -u
			fi
		fi
		rm -f "${env_script}"
	fi

	if [[ -f "${NCS_DIR}/zephyr/zephyr-env.sh" ]]; then
		# shellcheck disable=SC1091
		source "${NCS_DIR}/zephyr/zephyr-env.sh"
	fi

	export ZEPHYR_BASE="${ZEPHYR_BASE:-${NCS_DIR}/zephyr}"

	if command -v west >/dev/null 2>&1; then
		return 0
	fi

	# Last resort: west may only exist inside toolchain-manager launch
	if command -v nrfutil >/dev/null 2>&1; then
		if nrfutil toolchain-manager launch --ncs-version "${NCS_VERSION}" -- /bin/bash -c 'command -v west' >/dev/null 2>&1; then
			export NCS_USE_LAUNCH=1
			return 0
		fi
	fi

	echo "ERROR: west not found. Is nrfutil toolchain installed in this image?" >&2
	echo "PATH=${PATH}" >&2
	command -v nrfutil && nrfutil toolchain-manager list || true
	return 127
}

# Run a command with west available (handles launch fallback).
ncs_run() {
	ncs_activate || return $?
	if [[ "${NCS_USE_LAUNCH:-0}" == "1" ]]; then
		nrfutil toolchain-manager launch --ncs-version "${NCS_VERSION}" -- /bin/bash -lc "cd \"${NCS_DIR}\" && $*"
	else
		(
			cd "${NCS_DIR}" || exit 1
			eval "$@"
		)
	fi
}
