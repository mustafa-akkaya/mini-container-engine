# Mini Container Engine

    A minimal Linux container runtime written in C from scratch. This project demonstrates how modern container
  engines (like Docker) work under the hood using native Linux kernel features.

    ## Core Features

    - **Process Isolation (PID Namespace):** Container runs in an isolated process tree where it sees itself as PID 1
  using `clone(CLONE_NEWPID)`.
    - **Hostname Isolation (UTS Namespace):** Containers can have their own independent hostname using
  `clone(CLONE_NEWUTS)` and `sethostname()`.
    - **Filesystem Isolation (Mount Namespace):** Uses `pivot_root()` and private bind mounts to safely isolate the
  root filesystem without relying on `chroot()`.
    - **Network Isolation (Network Namespace):** Creates an isolated virtual network interface (`veth`) connected to
  a host Linux bridge (`br-container`) with NAT internet access via `iptables`.
    - **Resource Limiting (cgroups v2):** Enforces strict memory (`memory.max`), CPU (`cpu.max`), and process count
  (`pids.max`) limits.
    - **CLI & Container Management:** Supports listing active containers (`ps`), stopping containers (`stop`), and
  image management (`images`, `create-image`).

    ## Requirements

    - Linux Kernel 5.x+
    - GCC Compiler & Make utilities
    - Root privileges (`sudo`)
    - `iproute2` and `iptables` packages

    ## Quick Start

    1. Clone the repository and prepare the Alpine Linux root filesystem:

    ```bash
    git clone https://github.com/mustafa-akkaya/mini-container-engine.git
    cd mini-container-engine

    mkdir -p rootfs
    wget -O alpine-minirootfs.tar.gz https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/alpine-
  minirootfs-3.24.1-x86_64.tar.gz
    sudo tar -xzf alpine-minirootfs.tar.gz -C rootfs
    sudo cp /etc/resolv.conf rootfs/etc/resolv.conf
    sudo chroot rootfs /bin/sh -c "apk add --no-cache iproute2"

  2. Compile the project:

    ./build.sh

  ## Usage

  Run an interactive shell inside a basic container:

    sudo ./container run /bin/sh

  Run a container with custom hostname, network isolation, and resource limits (64MB RAM, max 10 PIDs):

    sudo ./container run --hostname webserver --net --memory 64 --pids 10 /bin/sh

  List running containers:

    sudo ./container ps

  Stop a running container by ID:

    sudo ./container stop <container-id>

  List available images:
    sudo ./container images

  Create a new image from the current filesystem state:

    sudo ./container create-image my-alpine

  ## Automated Testing & Demo

  Run the automated test suite to verify all isolation layers:

    sudo ./test.sh

  Run the step-by-step interactive demo:

    sudo ./demo.sh

  ## License

  MIT
