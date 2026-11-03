#!/system/bin/sh
# smart wrapper: privilege fallback chain
T=/data/local/tmp
OUT=$T/smart_out.txt
BAK=$T/smart_denied.txt

$T/smart_arm > $OUT 2>&1
if grep -q "Permission denied" $OUT 2>/dev/null; then
  cp $OUT $BAK
  su -c "$T/smart_arm" > $OUT 2>&1
  su 0 "$T/smart_arm" > $OUT 2>&1
  if grep -q "Permission denied" $OUT 2>/dev/null || [ ! -s $OUT ]; then
    cat $BAK > $OUT
  fi
fi
cat $OUT
