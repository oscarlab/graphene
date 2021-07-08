Meson conventions
=================

.. This guide should be reviewed when deprecating support for Ubuntu 18.04,
   maybe in 2022 Q2.

Graphene uses `Meson <https://mesonbuild.com>`__ as the buildsystem. Generally
we require Meson 0.51 to build from repository, but for compatibility with
Ubuntu 18.04 we aim to support building from source tarball using Meson 0.45.
To that end, we need to stick to a |~| particular convention with `wraps
<https://mesonbuild.com/Wrap-dependency-system-manual.html>`__.

Wrap filenames should be named ``<dirname>.wrap``, where ``<dirname>`` is the
name of the directory that will be unpacked from the source archive
(``source_filename``). Wrap should contain ``directory`` directive, which should
repeat the directory name (according to Meson documentation this is redundant,
but I |~| encountered a |~| circumstance, in which this was required).
