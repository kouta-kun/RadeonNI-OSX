#!/bin/sh
# Test g5/linux/install.sh and uninstall.sh against a fake nvram command and a
# plain directory (no Mac needed). Run from the repository: sh tests/linux_install.sh
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
pkg=$t/pkg; mkdir -p $pkg $t/vol $t/bin
cp $root/g5/linux/*.sh $pkg/
echo ELF > $pkg/rdnk.elf
python3 -I $root/scripts/of-hook.py block --dir '\RadeonNI' --dev '@OFDEV@' > $pkg/of-block.tmpl
# fake powerpc-utils nvram keeping variables in files
cat > $t/bin/nvram <<'F'
#!/bin/sh
d=$FAKE_NVRAM
case "$1" in
--print-config=*) n=${1#--print-config=}; cat "$d/$n" 2>/dev/null || true ;;
--update-config) n=${2%%=*}; printf '%s' "${2#*=}" > "$d/$n" ;;
esac
F
chmod +x $t/bin/nvram
export FAKE_NVRAM=$t/nv RADEONNI_STATE=$t/state PATH=$t/bin:$PATH
mkdir $FAKE_NVRAM
printf '%s' 'my-word  ( mine )' > $FAKE_NVRAM/nvramrc; printf false > "$FAKE_NVRAM/use-nvramrc?"
# the installer insists on PowerPC; the test fakes uname
printf '#!/bin/sh\n[ "$1" = -m ] && echo ppc64 || /usr/bin/uname "$@"\n' > $t/bin/uname; chmod +x $t/bin/uname
printf '#!/bin/sh\n[ "$1" = -u ] && echo 0 || /usr/bin/id "$@"\n' > $t/bin/id; chmod +x $t/bin/id
ok() { echo "ok   $*"; }; bad() { echo "FAIL $*"; exit 1; }

sh $pkg/install.sh --mountpoint $t/vol --of-device hd:3, --yes >/dev/null
[ -f $t/vol/RadeonNI/rdnk.elf ] || bad "client not copied"
nv=$(cat $FAKE_NVRAM/nvramrc)
case "$nv" in "my-word  ( mine ) ( RadeonNI-OF begin )"*"hd:3,\\RadeonNI\\rdnk.elf"*"( RadeonNI-OF end )") ok install keeps old text, device substituted;; *) bad "nvramrc: $nv";; esac
case "$nv" in *@OFDEV@*|*\\*"
"*) bad placeholder left;; esac
[ "$(cat "$FAKE_NVRAM/use-nvramrc?")" = true ] && ok use-nvramrc? true || bad use
sh $pkg/install.sh --mountpoint $t/vol --of-device hd:3, --yes >/dev/null
[ "$(printf '%s' "$(cat $FAKE_NVRAM/nvramrc)" | grep -o 'RadeonNI-OF begin' | wc -l)" = 1 ] && ok second install replaces the block || bad "block twice"
sh $pkg/install.sh --mountpoint $t/vol --of-device hd:3 --yes >/dev/null 2>&1 && bad "device without comma accepted" || ok device without comma refused
sh $pkg/uninstall.sh >/dev/null
[ "$(cat $FAKE_NVRAM/nvramrc)" = 'my-word  ( mine )' ] && ok uninstall restores nvramrc || bad "after uninstall: $(cat $FAKE_NVRAM/nvramrc)"
[ "$(cat "$FAKE_NVRAM/use-nvramrc?")" = true ] && ok use-nvramrc? left alone when other text remains || bad use2
# the Tiger block is unchanged by the --dev option
a=$(python3 -I $root/scripts/of-hook.py block --dir '\Library\RadeonNI\OpenFirmware')
case "$a" in *"load hd:,\\Library\\RadeonNI\\OpenFirmware\\rdnk.elf"*) ok Tiger block unchanged;; *) bad tiger block;; esac
# empty nvramrc: uninstall puts use-nvramrc? back to what it was
: > $FAKE_NVRAM/nvramrc; printf false > "$FAKE_NVRAM/use-nvramrc?"; rm -rf $t/state
sh $pkg/install.sh --mountpoint $t/vol --of-device hd:2, --yes >/dev/null
sh $pkg/uninstall.sh >/dev/null
[ ! -s $FAKE_NVRAM/nvramrc ] && [ "$(cat "$FAKE_NVRAM/use-nvramrc?")" = false ] && ok uninstall from empty restores both || bad empty
[ ! -e $t/vol/RadeonNI ] && ok client removed || bad "client left"
