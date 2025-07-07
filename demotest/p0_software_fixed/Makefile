


all:
	make -C kernel all 
	make -C user all 
	make -C user/cli all 
	make -C user/cli/sample all
	
app:
	make -C user all 
	make -C user/cli all 
	make -C user/cli/sample all
	
load:
	modprobe uio 
	insmod kernel/driver/kmod/mcdma-custom-driver/ifc_uio.ko 
	
clean:
	make -C kernel clean 
	make -C user clean 
	make -C user/cli clean 
	make -C user/cli/sample clean
	
	
	