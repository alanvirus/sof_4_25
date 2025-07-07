#ifndef _IFC_PERFQ_H_
#define _IFC_PERFQ_H_

#define PCI_BAR_MAX      6 

struct ifc_pci_resource {
        __u64 len;
        void *map;
};

struct ifc_pci_device { 
        int uio_fd;
        uint8_t uio_id;
        struct ifc_pci_resource r[6];
};

struct ifc_pci_id {
        __u16 vend;
        __u16 devid;
        __u16 subved;
        __u16 subdev;
};

void show_help(void);

#endif
