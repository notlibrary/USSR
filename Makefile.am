SUBDIRS = src

EXTRA_DIST = src/ural

dist-hook:
	set -eu; \
	build_dir=$$(mktemp -d); \
	state_dir=$$(mktemp -d); \
	trap 'rm -rf "$$build_dir" "$$state_dir"' EXIT; \
	cmake -S "$(abs_srcdir)/src/ural" -B "$$build_dir" \
	  -DURAL_STATIC_DEPS=OFF -DBUILD_TESTING=OFF; \
	cmake --build "$$build_dir" --parallel; \
	HOME="$$state_dir" "$$build_dir/ural" init; \
	cp "$$build_dir/ural" "$(distdir)/ural"; \
	cp "$$state_dir/.ussr/ussr.boxes" "$(distdir)/ussr.boxes"
