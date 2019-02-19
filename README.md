# ebpf-test

This is a utility for testing eBPF program offloading.

It requires two devices. The first is, naturally, the device that
will execute the offloaded code, such as this [QEMU device](https://github.com/sbates130272/qemu/tree/dev/martin/bpf)
(which uses this [kernel driver](https://github.com/iomartin/pci_ubpf_driver)).
It must support the [p2pmem framework](https://github.com/Eideticom/p2pmem-pci/tree/pci-p2p-4.20.x)).

The other is standard NVMe SSD.

This utility does the following steps:

1. Copy a binary file (`mem.dat`) to the SSD.
2. Load an eBPF program (`prog.o`) to the eBPF-capable device.
3. DMA (`chunk_bytes`) from the SSD to the eBPF device.
4. Execute the program.
5. Repeat 3-4 `chunk` times.

# Dependencies
[libebpf-offload](https://github.com/iomartin/libebpf-offload)

# Example

1. First, create an eBPF program
    ```sh
    $ echo "int func (int *mem) { return mem[1]; }" > prog.c
    $ clang -O2 -target bpf -c prog.c -o prog.o
    ```
2. Then, create a data file
    ```sh
    $ dd if=/dev/urandom of=mem.dat bs=4096 count=10
    ```
3. Run the utility:
    ```sh
    $ sudo ./ebpf-test /dev/nvme0n1 /dev/p2pmem0 /dev/pci_ubpf0 --prog prog.o --data mem.dat --chunk_size 4096  --chunks 10
    [...]
    Iter    Result
    0       0x03e6c532
    1       0xa3e0570f
    2       0x145522ad
    3       0xe62d37fb
    4       0xb8b56f19
    5       0x8282e378
    6       0xd63bc31b
    7       0x769030e5
    8       0x020e5231
    9       0x8d0a20a2
    Elapsed time: 0.008556s
    ```
4. Compare with the data file:
    ```sh
    $ for offset in `seq 4 4096 36868` ; do hexdump mem.dat --skip $offset --length 4 | head -n1; done
    0000004 c532 03e6
    0001004 570f a3e0
    0002004 22ad 1455
    0003004 37fb e62d
    0004004 6f19 b8b5
    0005004 e378 8282
    0006004 c31b d63b
    0007004 30e5 7690
    0008004 5231 020e
    0009004 20a2 8d0a
    ```
