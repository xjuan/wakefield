tests = [
  'test-compositor',
  'test-embedded',
  'test-embedding'
]

foreach test_file: tests

  executable(test_file, '@0@.c'.format(test_file),
    include_directories: top_inc,
    dependencies: wakefield_deps,
    link_with: wakefield_lib,
    install: false,
  )

endforeach

