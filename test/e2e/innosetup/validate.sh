#!/usr/bin/env bash
set -euo pipefail

innoextract=
installers=
command_timeout=${INNOEXTRACT_E2E_TIMEOUT:-120}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--innoextract)
			innoextract=$2
			shift 2
			;;
		--installers)
			installers=$2
			shift 2
			;;
		*)
			echo "unknown argument: $1" >&2
			exit 2
			;;
	esac
done

if [ -z "$innoextract" ] || [ ! -f "$innoextract" ]; then
	echo "innoextract binary missing: $innoextract" >&2
	exit 1
fi

if [ -z "$installers" ] || [ ! -d "$installers" ]; then
	echo "installer artifact directory missing: $installers" >&2
	exit 1
fi

fixture_dir=$(cd "$(dirname "$0")" && pwd)
tmp_dirs=()
expected_paths=(
	"app/hello.txt"
	"app/nested/config.ini"
	"app/data/script-data.txt"
)
expected_sources=(
	"$fixture_dir/payload/hello.txt"
	"$fixture_dir/payload/nested/config.ini"
	"$fixture_dir/payload/script-data.txt"
)

cleanup() {
	for dir in "${tmp_dirs[@]}"; do
		rm -rf "$dir"
	done
}
trap cleanup EXIT

run() {
	local label=$1
	shift
	local output
	local status
	echo "$label" >&2
	set +e
	if command -v timeout >/dev/null 2>&1; then
		output=$(timeout "${command_timeout}s" "$@" 2>&1)
	else
		output=$("$@" 2>&1)
	fi
	status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		printf '%s\n' "$output"
		if [ "$status" -eq 124 ]; then
			echo "command timed out after ${command_timeout}s: $*" >&2
		else
			echo "command failed: $*" >&2
		fi
		return "$status"
	fi
	printf '%s\n' "$output"
}

validate_installer() {
	local artifact_dir=$1
	local compiler=$2
	local data_version=$3
	local installer_name=$4
	local installer="$artifact_dir/$installer_name"
	local tmp
	local actual_version
	local listing
	local output_dir

	if [ ! -f "$installer" ]; then
		echo "$compiler: installer artifact missing: $installer" >&2
		return 1
	fi

	tmp=$(mktemp -d)
	tmp_dirs+=("$tmp")

	actual_version=$(run "$compiler: data-version" "$innoextract" --progress=0 --data-version --silent "$installer" | tr -d '\r\n')
	if [ "$actual_version" != "$data_version" ]; then
		echo "$compiler: expected data version '$data_version', got '$actual_version'" >&2
		return 1
	fi

	run "$compiler: info" "$innoextract" --progress=0 --info "$installer" >/dev/null

	listing="$tmp/listing.txt"
	run "$compiler: list" "$innoextract" --progress=0 --list --silent "$installer" | tr '\\' '/' | tr -d '\r' > "$listing"
	for path in "${expected_paths[@]}"; do
		if ! grep -Fxq "$path" "$listing"; then
			echo "$compiler: --list output missed expected path: $path" >&2
			return 1
		fi
	done

	run "$compiler: test" "$innoextract" --progress=0 --test --silent "$installer" >/dev/null

	output_dir="$tmp/output"
	run "$compiler: extract" "$innoextract" --progress=0 --extract --silent --output-dir "$output_dir" "$installer" >/dev/null
	for i in "${!expected_paths[@]}"; do
		local extracted="$output_dir/${expected_paths[$i]}"
		local expected="${expected_sources[$i]}"
		if [ ! -f "$extracted" ]; then
			echo "$compiler: extracted file missing: ${expected_paths[$i]}" >&2
			return 1
		fi
		if ! cmp -s "$expected" "$extracted"; then
			echo "$compiler: extracted content mismatch: ${expected_paths[$i]}" >&2
			return 1
		fi
	done

	output_dir="$tmp/output-code"
	run "$compiler: extract-code" "$innoextract" --progress=0 --extract --extract-code --silent --output-dir "$output_dir" "$installer" >/dev/null
	local compiled_code="$output_dir/embedded/CompiledCode.bin"
	if [ ! -s "$compiled_code" ]; then
		echo "$compiler: compiled [Code] script missing: embedded/CompiledCode.bin" >&2
		return 1
	fi
	if ! grep -aq "SHOULDINSTALL" "$compiled_code"; then
		echo "$compiler: compiled [Code] script lacks the ShouldInstall function" >&2
		return 1
	fi
	if [ -e "$tmp/output/embedded" ]; then
		echo "$compiler: compiled [Code] script written without --extract-code" >&2
		return 1
	fi

	echo "$compiler: $data_version ok"
}

manifests=()
while IFS= read -r -d '' manifest; do
	manifests+=("$manifest")
done < <(find "$installers" -type f -name 'manifest-*.tsv' -print0 | sort -z)

if [ "${#manifests[@]}" -eq 0 ]; then
	echo "no manifests found in $installers" >&2
	exit 1
fi

for manifest in "${manifests[@]}"; do
	compiler=
	data_version=
	installer_name=
	if ! IFS=$'\t' read -r compiler data_version installer_name < "$manifest"; then
		if [ -z "$compiler" ] && [ -z "$data_version" ] && [ -z "$installer_name" ]; then
			echo "could not read manifest: $manifest" >&2
			exit 1
		fi
	fi
	compiler=${compiler//$'\r'/}
	data_version=${data_version//$'\r'/}
	installer_name=${installer_name//$'\r'/}
	if [ -z "$compiler" ] || [ -z "$data_version" ] || [ -z "$installer_name" ]; then
		echo "invalid manifest: $manifest" >&2
		exit 1
	fi
	validate_installer "$(dirname "$manifest")" "$compiler" "$data_version" "$installer_name"
done
