#!/bin/sh 

modprobe uio 
insmod kernel/driver/kmod/mcdma-custom-driver/ifc_uio.ko
	
