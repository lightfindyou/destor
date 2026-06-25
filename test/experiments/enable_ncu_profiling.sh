#!/bin/sh
# 为当前 Linux 主机开放 NCU / Nsight 性能计数器（非 root 用户可用）。
# 需要 root：sudo ./enable_ncu_profiling.sh
#
# 原理：设置 NVreg_RestrictProfilingToAdminUsers=0
# 验证：grep RmProfilingAdminOnly /proc/driver/nvidia/params  # 应为 0
#
# 生效方式：推荐 reboot；或在不使用 GPU 的维护窗口内 reload 驱动模块。
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "请使用 root 运行: sudo $0" >&2
	exit 1
fi

CONF=/etc/modprobe.d/nvidia-ncu-profiling.conf
cat >"$CONF" <<'EOF'
# Allow non-admin users to access GPU performance counters (NCU / nvprof).
# See: https://developer.nvidia.com/nvidia-development-tools-solutions-ERR_NVGPUCTRPERM-permission-issue-performance-counters
options nvidia NVreg_RestrictProfilingToAdminUsers=0
options nvidia_drm NVreg_RestrictProfilingToAdminUsers=0
options nvidia_modeset NVreg_RestrictProfilingToAdminUsers=0
options nvidia_uvm NVreg_RestrictProfilingToAdminUsers=0
EOF
chmod 644 "$CONF"
echo "[ok] wrote $CONF"

if command -v update-initramfs >/dev/null 2>&1; then
	echo "[initramfs] update-initramfs -u"
	update-initramfs -u
elif command -v dracut >/dev/null 2>&1; then
	echo "[initramfs] dracut --force"
	dracut --force
fi

echo ""
echo "当前 RmProfilingAdminOnly=$(grep -E '^RmProfilingAdminOnly:' /proc/driver/nvidia/params 2>/dev/null | awk '{print $2}' || echo '?')"
echo ""
echo "配置已写入，需重启或重载 NVIDIA 模块后生效。"
echo "  推荐: sudo reboot"
echo ""
echo "重启后验证:"
echo "  grep RmProfilingAdminOnly /proc/driver/nvidia/params   # 期望 0"
echo "  ncu --query-metrics | head -3"
echo ""
echo "重启后可直接运行: ./run_exp4_profiling.sh"
