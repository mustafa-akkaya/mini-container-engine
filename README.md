# Mini Container Engine

    A minimal container runtime built from scratch in C, implementing core Linux containerization primitives. This
  project demonstrates how Docker-like containers work under the hood using Linux kernel features.

    ## Architecture


  ┌─────────────────────────────────────────────────────┐
  │                    HOST SYSTEM                       │
  │                                                     │
  │  ┌───────────────┐     ┌──────────────────────────┐ │
  │  │ ./container   │     │    Linux Kernel           │ │
  │  │    CLI        │────▶│  ┌─────────────────────┐  │ │
  │  │               │     │  │ clone() syscall      │  │ │
  │  │  run          │     │  │  ├─ CLONE_NEWPID     │  │ │
  │  │  ps           │     │  │  ├─ CLONE_NEWUTS     │  │ │
  │  │  stop         │     │  │  ├─ CLONE_NEWNS      │  │ │
  │  │  images       │     │  │  └─ CLONE_NEWNET     │  │ │
  │  └───────────────┘     │  └─────────────────────┘  │ │
  │                        │                            │ │
  │  ┌─────────────────────┤  ┌──────────────────────┐  │ │
  │  │   br-container      │  │   cgroups v2         │  │ │
  │  │   10.0.0.1/24       │  │  ├─ memory.max      │  │ │
  │  │       │             │  │  ├─ cpu.max          │  │ │
  │  │    ┌──┴──┐          │  │  └─ pids.max         │  │ │
  │  │    │veth0│          │  └──────────────────────┘  │ │
  │  └────┴──┬──┴──────────┘                            │ │
  │          │ (veth pair)                               │ │
  │  ════════╪══════════════════════════════════════════ │ │
  │          │    CONTAINER (isolated)                   │ │
  │  ┌───────┴─────────────────────────────────────────┐│ │
  │  │  ┌──────┐  ┌──────────┐  ┌───────────────────┐ ││ │
  │  │  │veth1 │  │ /proc    │  │ Alpine Linux      │ ││ │
  │  │  │10.0. │  │ (mount)  │  │ rootfs            │ ││ │
  │  │  │0.2/24│  │          │  │ (pivot_root)      │ ││ │
  │  │  └──────┘  └──────────┘  └───────────────────┘ ││ │
  │  │  PID: 1    Hostname: mini-container             ││ │
  │  └─────────────────────────────────────────────────┘│ │
  └─────────────────────────────────────────────────────┘

    ## Features

    | Feature | Implementation | Linux Primitive |
    |---------|---------------|-----------------|
    | **Process Isolation** | Container gets its own PID namespace, sees itself as PID 1 | `clone(CLONE_NEWPID)` |
    | **Hostname Isolation** | Each container can have its own hostname | `clone(CLONE_NEWUTS)` + `sethostname()` |
    | **Filesystem Isolation** | Container sees only its own root filesystem | `pivot_root()` + `mount()` |
    | **Network Isolation** | Container gets its own IP address and network stack | `clone(CLONE_NEWNET)` + veth +
  bridge |
    | **Resource Limiting** | Memory, CPU, and process count limits | cgroups v2 |
    | **Container Management** | List running containers, stop by ID | `/tmp/mini-container/` state tracking |
    | **Image Management** | Create and list filesystem images | Directory-based images |
    | **Internet Access** | Container can reach the internet via NAT | `iptables` MASQUERADE |

    ## How It Works

    ### 1. Process Isolation (`clone()`)
    The runtime uses the `clone()` system call with namespace flags to create an isolated process. Unlike `fork()`,
  `clone()` can create new namespaces:

    ```c
    int clone_flags = CLONE_NEWUTS    // Own hostname
                   | CLONE_NEWPID    // Own PID space (PID 1)
                   | CLONE_NEWNS     // Own mount table
                   | CLONE_NEWNET    // Own network stack
                   | SIGCHLD;

    pid_t child = clone(container_main, stack + STACK_SIZE, clone_flags, &config);

  ### 2. Filesystem Isolation ( pivot_root() )
  Instead of  chroot()  (which is escapable), we use  pivot_root()  for true filesystem isolation:
    mount(rootfs, rootfs, NULL, MS_BIND | MS_PRIVATE, NULL);  // Bind mount
    pivot_root(rootfs, old_root);                               // Swap root
    umount2("/old_root", MNT_DETACH);                          // Remove old root

  ### 3. Resource Limiting (cgroups v2)

  The runtime writes to  /sys/fs/cgroup/  to enforce resource limits:
    // Limit memory to 64MB
    write("/sys/fs/cgroup/mini-container/memory.max", "67108864");

    // Limit CPU to 50%
    write("/sys/fs/cgroup/mini-container/cpu.max", "50000 100000");

    // Limit to 10 processes
    write("/sys/fs/cgroup/mini-container/pids.max", "10");

  ### 4. Network Isolation (veth + bridge + NAT)
    Host: br-container (10.0.0.1) ──── veth0 ═══╗
                                                  ║ veth pair
    Container:                         veth1 ═══╝ (10.0.0.2)
                                        │
                                        └── default route → 10.0.0.1
                                               │
                                               └── iptables NAT → internet

  ## Requirements

  • Linux kernel 5.x+ (tested on Ubuntu 24.04+)
  • GCC compiler
  • Root privileges ( sudo )
  •  iproute2  and  iptables  (for network isolation)

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

    # Install iproute2 in rootfs (for network commands inside container)
    sudo chroot rootfs /bin/sh -c "apk add --no-cache iproute2"

    # Build
    ./build.sh

    # Run your first container!
    sudo ./container run /bin/sh

  ## Usage
  ### Basic container
    sudo ./container run /bin/sh

  ### Custom hostname

    sudo ./container run --hostname webserver /bin/sh

  ### With network isolation
    sudo ./container run --net /bin/sh

    # Inside the container:
    ip addr          # See container's own IP (10.0.0.2)
    ping 8.8.8.8     # Internet access works!

  ### With resource limits
    sudo ./container run --memory 64 --cpu 50 --pids 10 /bin/sh

  ### Full isolation (all features combined)

    sudo ./container run \
      --hostname production \
      --net \
      --memory 128 \
      --cpu 50 \
      --pids 20 \
      /bin/sh

  ### Container management
    # List running containers
    sudo ./container ps

    # Stop a container by ID
    sudo ./container stop <container-id>

    # List available images
    sudo ./container images

    # Create a new image from rootfs
    sudo ./container create-image my-alpine

  ## Testing
    # Run automated test suite (14 tests)
    sudo ./test.sh

    # Run interactive demo (shows all features step by step)
    sudo ./demo.sh

  ## Project Structure
    mini-container-engine/
    ├── main.c            # CLI entry point, argument parsing, container management
    ├── runtime.c         # Core runtime: namespaces, filesystem, cgroups, networking
    ├── container.h       # Shared type definitions and function declarations
    ├── build.sh          # Build script
    ├── demo.sh           # Interactive demo showcasing all features
    ├── test.sh           # Automated test suite (14 tests)
    ├── rootfs/           # Alpine Linux root filesystem (not tracked in git)
    └── images/           # User-created container images (not tracked in git)

  ## Docker vs Mini Container Engine
   Capability                           | Docker                               | Mini Container Engine
  --------------------------------------|--------------------------------------|-------------------------------------
   PID namespace                        | ✅                                   | ✅
   UTS namespace                        | ✅                                   | ✅
   Mount namespace                      | ✅                                   | ✅
   Network namespace                    | ✅                                   | ✅
   cgroups                              | ✅                                   | ✅
   Union filesystem (OverlayFS)         | ✅                                   | ❌
   Container registry (pull/push)       | ✅                                   | ❌
   User namespace                       | ✅                                   | ❌
   Seccomp filters                      | ✅                                   | ❌
   Multi-container networking           | ✅                                   | ❌
  ## Key Linux Concepts Used
   Concept                              | Purpose                              | Man Page
  --------------------------------------|--------------------------------------|-------------------------------------
    clone(2)                            | Create process with new namespaces   |  man 2 clone
    pivot_root(2)                       | Change root filesystem               |  man 2 pivot_root
    mount(2)                            | Mount /proc, /sys filesystems        |  man 2 mount
    sethostname(2)                      | Set container hostname               |  man 2 sethostname
    cgroups v2                          | Resource limiting                    |  man 7 cgroups
    veth(4)                             | Virtual ethernet pair                |  man 4 veth
    iptables(8)                         | NAT for internet access              |  man 8 iptables

  ## License

  MIT
