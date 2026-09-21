# Bullet 2.75 for MikuDanceStudio

`bullet-src/src` is the source tree used by this project's local Conan recipe.
It is a modified Bullet 2.75 distribution, not an unmodified upstream release.
It includes the existing MMD floating-point/solver reconstruction changes and
optional environment-controlled diagnostics. Source copyright/license notices
are preserved. See `LICENSE.txt` and the source-file comments.

The tree was previously local-only and ignored by Git. It is now versioned so
clean checkouts and release runners build the same physics dependency. Do not
substitute a newer Bullet release or an unmodified 2.75 archive.
