QEMU=qemu-system-riscv64
DISK=../lkm/impl/arceos_ex/build/virtio-blk.raw

if [ $# -ne 1 ]; then
  APP=/sbin/init
else
  APP=$1
fi

${QEMU} -m 128M -smp 1 -machine virt \
-bios default -kernel ./arch/riscv/boot/Image \
-device virtio-blk-device,drive=disk0 -drive id=disk0,if=none,format=raw,file=${DISK} \
-device virtio-net-device,netdev=net0 -netdev user,id=net0,hostfwd=tcp::5555-:5555 \
-nographic -append "earlycon=sbi root=/dev/vda rw console=ttyS0 init=$APP"
