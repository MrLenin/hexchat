# Electron frontend WebSocket backend
# Requires libwebsockets and jansson for WebSocket server and JSON

libwebsockets_dep = dependency('libwebsockets', version: '>= 4.0', required: true)
jansson_dep = dependency('jansson', version: '>= 2.10', required: true)

hexchat_electron_sources = [
  'fe-electron.c',
  'ws-server.c',
  'json-protocol.c',
]

hexchat_electron_deps = [
  hexchat_common_dep,
  libwebsockets_dep,
  jansson_dep,
]

executable('hexchat-electron',
  sources: hexchat_electron_sources,
  dependencies: hexchat_electron_deps,
  install: true,
)
