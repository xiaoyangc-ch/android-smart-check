#!/system/bin/sh
# smart wrapper: privilege fallback chain
T=/data/local/tmp
OUT=$T/smart_out.txt
BAK=$T/smart_denied.txt

$T/smart_arm > $OUT 2>&1
if grep -q "Permission denied" $OUT 2>/dev/null; then
  cp $OUT $BAK
  # 每次提权尝试独立判定(退出码+输出双重校验), 成功即停, 避免失败覆盖成功输出
  su -c "$T/smart_arm" > $OUT 2>&1
  if [ $? -ne 0 ] || grep -q "Permission denied" $OUT 2>/dev/null || [ ! -s $OUT ]; then
    su 0 "$T/smart_arm" > $OUT 2>&1
    if [ $? -ne 0 ] || grep -q "Permission denied" $OUT 2>/dev/null || [ ! -s $OUT ]; then
      cat $BAK > $OUT
    fi
  fi
fi
cat $OUT
