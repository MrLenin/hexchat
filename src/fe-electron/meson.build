# Electron frontend WebSocket backend
# Requires libwebsockets (from common) and jansson for JSON protocol

# Ensure libwebsockets was found (should be enabled automatically)
if not libwebsockets_dep.found()
  error('Electron frontend requires libwebsockets. Enable with-libwebsockets option.')
endif

jansson_dep = dependency('jansson', version: '>= 2.10', required: true)

hexchat_electron_sources = [
  'fe-electron.c',
  'json-protocol.c',
]

hexchat_electron_deps = [
  hexchat_common_dep,
  jansson_dep,
]

executable('hexchat-electron',
  sources: hexchat_electron_sources,
  dependencies: hexchat_electron_deps,
  install: true,
)
