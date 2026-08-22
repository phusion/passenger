#!/usr/bin/env bash
set -x
VERSION=1.49.2
#apt update && apt install -y build-essential curl libtool autoconf
rm -rf src/cxx_supportlib/vendor-copy/libuv
curl -sSL "https://dist.libuv.org/dist/v${VERSION}/libuv-v${VERSION}.tar.gz" | tar -C src/cxx_supportlib/vendor-copy/ -xz
mv src/cxx_supportlib/vendor-copy/libuv-* src/cxx_supportlib/vendor-copy/libuv
cd src/cxx_supportlib/vendor-copy/libuv || exit 1
if [ "$(uname)" = Darwin ]; then
sed -e 's/AC_CONFIG_LINKS(\[test/# &/g' -i '' configure.ac
else
sed -e 's/AC_CONFIG_LINKS(\[test/# &/g' -i'' configure.ac
fi
./autogen.sh
rm -rf test docs img tools .github CMakeLists.txt cmake-toolchains src/win autogen.sh .readthedocs.yaml .mailmap
find . \( -name '*.md' -o -name '.git*' \) -delete
