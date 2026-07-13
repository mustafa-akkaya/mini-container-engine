# Mini Container Engine

    A minimal container runtime built from scratch in C, implementing core Linux containerization primitives. This
  project demonstrates how Docker-like containers work under the hood using Linux kernel features.

    ## Architecture

    ```mermaid
    graph TD
        subgraph Host["HOST SYSTEM"]
            CLI["./container CLI<br>run | ps | stop | images"]
            Kernel["Linux Kernel<br>clone() syscall"]
            CG["cgroups v2<br>memory.max | cpu.max | pids.max"]
            Bridge["br-container<br>10.0.0.1/24"]
            Veth0["veth0"]
        end

        subgraph Container["ISOLATED CONTAINER (PID 1)"]
            Veth1["veth1<br>10.0.0.2/24"]
            FS["Alpine Linux Rootfs<br>pivot_root()"]
            Proc["/proc & /sys<br>private mounts"]
        end

        CLI -->|spawns via clone| Kernel
        Kernel -->|CLONE_NEWPID / NEWUTS / NEWNS / NEWNET| Container
        CG -.->|limits CPU/RAM/PIDs| Container
        Bridge --- Veth0
        Veth0 -.->|veth peer| Veth1
        Veth1 -->|NAT via iptables| Internet((Internet))

    +-----------------------------------------------------------------+
    | HOST SYSTEM                                                     |
    |                                                                 |
    |  [ ./container CLI ] ---> clone(NEWPID|NEWUTS|NEWNS|NEWNET)     |
    |                                 |                               |
    |  [ cgroups v2 ] ----> Limits: RAM, CPU, PIDs                    |
    |  [ br-container ] --> 10.0.0.1/24 (Bridge)                      |
    |                            |                                    |
    +----------------------------|------------------------------------+
                                 | veth pair
    +----------------------------|------------------------------------+
    | CONTAINER                  v                                    |
    |  [ veth1: 10.0.0.2 ] ---> Default Gateway: 10.0.0.1 (NAT)       |
    |  [ Alpine Rootfs ] -----> Isolated via pivot_root()             |
    |  [ Hostname ] ----------> mini-container                        |
    |  [ PID 1 ] -------------> Isolated Process Tree                 |
    +-----------------------------------------------------------------+

  ## Features
   Feature              | Implementation                                      | Linux Primitive
  ----------------------|-----------------------------------------------------|--------------------------------------
   Process Isolation    | Container gets its own PID namespace, sees itself   |  clone(CLONE_NEWPID)
                        | as PID 1                                            |
   Hostname Isolation   | Each container can have its own hostname            |  clone(CLONE_NEWUTS)  +
                        |                                                     | sethostname()
   Filesystem Isolation | Container sees only its own root filesystem         |  pivot_root()  +  mount()
   Network Isolation    | Container gets its own IP address and network stack |  clone(CLONE_NEWNET)  + veth +
                        |                                                     | bridge
   Resource Limiting    | Memory, CPU, and process count limits               | cgroups v2
   Container Management | List running containers, stop by ID                 |  /tmp/mini-container/  state
                        |                                                     | tracking
   Image Management     | Create and list filesystem images                   | Directory-based images
   Internet Access      | Container can reach the internet via NAT            |  iptables  MASQUERADE

  ## Quick Start

    # Clone the repository
    git clone https://github.com/mustafa-akkaya/mini-container-engine.git
    cd mini-container-engine

    # Download Alpine Linux rootfs
    mkdir -p rootfs
    wget -O alpine-minirootfs.tar.gz \
      https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/x86_64/alpine-minirootfs-3.24.1-x86_64.tar.gz
    sudo tar -xzf alpine-minirootfs.tar.gz -C rootfs
    sudo cp /etc/resolv.conf rootfs/etc/resolv.conf

    # Install iproute2 in rootfs
    sudo chroot rootfs /bin/sh -c "apk add --no-cache iproute2"

    # Build
    ./build.sh

    # Run container
    sudo ./container run /bin/sh

  ## Usage Examples

    # Basic interactive shell
    sudo ./container run /bin/sh

    # Run with custom hostname, networking and resource limits
    sudo ./container run --hostname production --net --memory 64 --pids 10 /bin/sh

    # List running containers
    sudo ./container ps

    # Stop a container
    sudo ./container stop <container-id>

    # Run automated demo & tests
    sudo ./test.sh
    sudo ./demo.sh

  ## License

  MIT
