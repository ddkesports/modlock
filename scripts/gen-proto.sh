#!/usr/bin/env bash
# Generate Go/TypeScript protocols and C++ messages exported by the SDK DLL.
#
# protoc runs in-process through go-protoc-wasi (no native protoc build). The
# WASI protoc cannot pass --cpp_out=dllexport_decl, so restore-proto-exports.py
# re-applies the MODLOCK_API markers after generation.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -e vendor ]]; then
  echo 'Generation needs an unvendored checkout; retain or relocate vendor first.' >&2
  exit 1
fi

GOFLAGS=-mod=mod go mod vendor
trap 'rm -rf vendor' EXIT

GOFLAGS=-mod=mod go run -mod=mod -tags=purego   github.com/aperturerobotics/common/cmd/aptre generate \
  --language cpp --language go --language ts \
  --targets './proto/modlock/*.proto' "$@"

python3 scripts/restore-proto-exports.py

GOFLAGS=-mod=mod go run ./cmd/proto-export
