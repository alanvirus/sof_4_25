
此处软件为官方的 MCDMA 配套驱动程序以及测试程序
此软件有小幅修改

相关上层程序需要巨页环境，可通过 grub  相关命令配置：

修改 /etc/default/grub:

GRUB_CMDLINE_LINUX 中添加: default_hugepagesz=1G hugepagesz=1G hugepages=8 iommu=off

sudo update-grub

重启

iommu 需要明确关闭，它会影响本软件程序的运行.

主目录编写了 Makefile，可以一次性 make 所有模块.
若个别程序没有生成，可以 cd 到其目录，make all

此软件驱动依赖 UIO 驱动，因此加载方式如下：

modprobe uio 
insmod kernel/driver/kmod/mcdma-custom-driver/ifc_uio.ko

关于一些测试程序的使用，可参考此目录的一些测试脚本

FPGA 会做内部回环，因此测试脚本可以进行收发测试，并比较收发内容

如下命令预先开启后台 DMA 收进程，然后开始 DMA 发操作：

user/cli/sample/avmm_rx  blksize=819200 qdeep=1024 count=1024 out=out.bin  &
sleep 1
user/cli/sample/avmm_tx  blksize=819200 qdeep=1024 count=1024 in=random.bin  &

测试将 random.bin 文件发出，收回的数据保存为 out.bin 文件.


