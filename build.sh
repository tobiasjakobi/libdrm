#!/bin/bash

source ${HOME}/local/build.environment

target="$(gcc -dumpmachine)"
builddir="build.${target}"
meson_args="--buildtype=plain --prefix=${HOME}/local"

if [[ ! -d "${builddir}" ]]; then
  mkdir "${builddir}"
  meson ${meson_args} "${builddir}" $(cat ../drm.config)
fi

meson --reconfigure ${meson_args} "${builddir}" $(cat ../drm.config)
ninja -C "${builddir}" clean
ninja -C "${builddir}" -v
