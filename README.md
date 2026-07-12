# Mini Container Engine (C)

Linux çekirdeğinin sunduğu temel izolasyon mekanizmalarını (namespaces, cgroups, chroot) kullanarak sıfırdan C ile geliştirilmiş, hafif bir container çalışma zamanı (runtime) projesi. 

## Projenin Amacı
Bu proje, Docker ve benzeri container teknolojilerinin arka planda işletim sistemi seviyesinde nasıl çalıştığını anlamak ve sistem programlama (system programming) yeteneklerini geliştirmek amacıyla yazılmıştır.

## Kullanılan Temel Mekanizmalar
* **Namespaces:** İşlem (process), ağ (network) ve bağlama (mount) izolasyonu.
* **Cgroups:** İşlemcinin (CPU) ve belleğin (RAM) kaynak sınırlandırması.
* **Chroot:** Kök dosya sisteminin (rootfs) hapsedilmesi.

## Sistem Mimarisi

```mermaid
graph TD
    A[Kullanıcı Girdisi] --> B(Mini Engine)
    B --> C{İzolasyon Kurulumu}
    C -- "clone()" --> D[Namespace İzolasyonu]
    C -- "cgroups" --> E[Kaynak Kısıtlaması]
    D --> F[chroot ile Kök Dizin]
    E --> F
    F --> G[İzole Edilmiş Process /bin/sh]
