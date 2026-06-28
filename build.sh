#cp arch/riscv/configs/defconfig .config
#make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- menuconfig
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j $(nproc)
#mkdir -p output
#make ARCH=riscv install INSTALL_PATH=./output
#make ARCH=riscv modules_install INSTALL_MOD_PATH=./output
