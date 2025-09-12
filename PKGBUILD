# Maintainer: liammmcauliffe <liam@example.com>
pkgname=hyprworm
pkgver=1.0.0
pkgrel=1
pkgdesc="A fast and lightweight window switcher for Hyprland built in C"
arch=('x86_64' 'aarch64')
url="https://github.com/liammmcauliffe/hyprworm"
license=('MIT')
depends=('cjson')
makedepends=('git' 'make' 'gcc')
source=("$pkgname-$pkgver.tar.gz::https://github.com/liammmcauliffe/hyprworm/archive/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$pkgname-$pkgver"
    make
}

package() {
    cd "$pkgname-$pkgver"
    make DESTDIR="$pkgdir" install
}
