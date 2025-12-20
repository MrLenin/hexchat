import { create } from 'zustand'

// Types matching the JSON protocol
export interface UserInfo {
  nick: string
  hostname: string | null
  realname: string | null
  account: string | null
  prefix: string
  away: boolean
  op: boolean
  hop: boolean
  voice: boolean
}

export interface SessionState {
  sessionId: string
  serverId: string | null
  name: string
  sessionType: 'server' | 'channel' | 'dialog' | 'notices' | 'snotices' | 'unknown'
  topic: string
  topicStripped: string
  messages: MessageItem[]
  users: Map<string, UserInfo>
  tabColor: 'none' | 'data' | 'message' | 'hilight'
}

export interface ServerState {
  serverId: string
  nick: string
  away: boolean
  lag: number
  connected: boolean
}

export interface MessageItem {
  text: string
  timestamp: number
  noActivity: boolean
}

interface HexChatStore {
  // Connection state
  ws: WebSocket | null
  connected: boolean

  // Data
  servers: Map<string, ServerState>
  sessions: Map<string, SessionState>
  activeSessionId: string | null
  updateCounter: number  // Increments on each state change to force re-renders

  // Actions
  connect: (port: number) => void
  disconnect: () => void
  setActiveSession: (sessionId: string) => void
  sendCommand: (sessionId: string, command: string) => void
  sendInput: (sessionId: string, text: string) => void

  // Internal handlers
  handleMessage: (event: MessageEvent) => void
}

export const useStore = create<HexChatStore>((set, get) => ({
  ws: null,
  connected: false,
  servers: new Map(),
  sessions: new Map(),
  activeSessionId: null,
  updateCounter: 0,

  connect: (port: number) => {
    // Prevent duplicate connections (React StrictMode calls useEffect twice)
    const existingWs = get().ws
    if (existingWs && existingWs.readyState !== WebSocket.CLOSED) {
      console.log('WebSocket already connected, skipping')
      return
    }

    const ws = new WebSocket(`ws://localhost:${port}`)

    ws.onopen = () => {
      console.log('WebSocket connected')
      set({ ws, connected: true })
    }

    ws.onclose = () => {
      console.log('WebSocket disconnected')
      set({ ws: null, connected: false })
      // Attempt reconnection after 3 seconds
      setTimeout(() => {
        if (!get().connected) {
          get().connect(port)
        }
      }, 3000)
    }

    ws.onerror = (err) => {
      console.error('WebSocket error:', err)
    }

    ws.onmessage = (event) => {
      get().handleMessage(event)
    }

    // Store the ws immediately to prevent race conditions
    set({ ws })
  },

  disconnect: () => {
    const { ws } = get()
    if (ws) {
      ws.close()
    }
    set({ ws: null, connected: false })
  },

  setActiveSession: (sessionId: string) => {
    set({ activeSessionId: sessionId })
    // Clear tab color when focusing
    const sessions = new Map(get().sessions)
    const session = sessions.get(sessionId)
    if (session) {
      sessions.set(sessionId, { ...session, tabColor: 'none' })
      set({ sessions })
    }
  },

  sendCommand: (sessionId: string, command: string) => {
    const { ws } = get()
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({
        type: 'command.execute',
        timestamp: Date.now(),
        payload: { sessionId, command }
      }))
    }
  },

  sendInput: (sessionId: string, text: string) => {
    const { ws } = get()
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({
        type: 'input.text',
        timestamp: Date.now(),
        payload: { sessionId, text }
      }))
    }
  },

  handleMessage: (event: MessageEvent) => {
    try {
      const msg = JSON.parse(event.data)
      const { type, payload } = msg

      switch (type) {
        case 'session.created': {
          const sessions = new Map(get().sessions)
          const newSession: SessionState = {
            sessionId: payload.sessionId,
            serverId: payload.serverId,
            name: payload.name,
            sessionType: payload.sessionType,
            topic: '',
            topicStripped: '',
            messages: [],
            users: new Map(),
            tabColor: 'none',
          }
          sessions.set(payload.sessionId, newSession)

          // Auto-focus if requested or first session
          const activeSessionId = payload.focus || !get().activeSessionId
            ? payload.sessionId
            : get().activeSessionId

          set({ sessions, activeSessionId })
          break
        }

        case 'session.closed': {
          const sessions = new Map(get().sessions)
          sessions.delete(payload.sessionId)

          // If active session was closed, switch to another
          let activeSessionId = get().activeSessionId
          if (activeSessionId === payload.sessionId) {
            const remaining = Array.from(sessions.keys())
            activeSessionId = remaining.length > 0 ? remaining[0] : null
          }

          set({ sessions, activeSessionId })
          break
        }

        case 'text.print': {
          const sessions = new Map(get().sessions)
          const session = sessions.get(payload.sessionId)
          if (session) {
            const newMessage: MessageItem = {
              text: payload.text,
              timestamp: payload.timestamp,
              noActivity: payload.noActivity,
            }
            sessions.set(payload.sessionId, {
              ...session,
              messages: [...session.messages, newMessage].slice(-1000), // Keep last 1000 messages
            })
            set({ sessions, updateCounter: get().updateCounter + 1 })
          }
          break
        }

        case 'channel.topic': {
          const sessions = new Map(get().sessions)
          const session = sessions.get(payload.sessionId)
          if (session) {
            sessions.set(payload.sessionId, {
              ...session,
              topic: payload.topic,
              topicStripped: payload.topicStripped,
            })
            set({ sessions })
          }
          break
        }

        case 'userlist.insert': {
          const sessions = new Map(get().sessions)
          const session = sessions.get(payload.sessionId)
          if (session) {
            const users = new Map(session.users)
            users.set(payload.user.nick, payload.user)
            sessions.set(payload.sessionId, { ...session, users })
            set({ sessions })
          }
          break
        }

        case 'userlist.remove': {
          const sessions = new Map(get().sessions)
          const session = sessions.get(payload.sessionId)
          if (session) {
            const users = new Map(session.users)
            users.delete(payload.nick)
            sessions.set(payload.sessionId, { ...session, users })
            set({ sessions })
          }
          break
        }

        case 'userlist.update': {
          const sessions = new Map(get().sessions)
          const session = sessions.get(payload.sessionId)
          if (session) {
            const users = new Map(session.users)
            users.set(payload.user.nick, payload.user)
            sessions.set(payload.sessionId, { ...session, users })
            set({ sessions })
          }
          break
        }

        case 'userlist.clear': {
          const sessions = new Map(get().sessions)
          const session = sessions.get(payload.sessionId)
          if (session) {
            sessions.set(payload.sessionId, { ...session, users: new Map() })
            set({ sessions })
          }
          break
        }

        case 'ui.tabcolor': {
          const sessions = new Map(get().sessions)
          const session = sessions.get(payload.sessionId)
          // Don't update color if this is the active session
          if (session && payload.sessionId !== get().activeSessionId) {
            sessions.set(payload.sessionId, {
              ...session,
              tabColor: payload.color,
            })
            set({ sessions })
          }
          break
        }

        case 'server.event': {
          const servers = new Map(get().servers)
          let server = servers.get(payload.serverId)
          if (!server) {
            server = {
              serverId: payload.serverId,
              nick: '',
              away: false,
              lag: 0,
              connected: false,
            }
          }
          server = {
            ...server,
            connected: payload.event === 'connected' || payload.event === 'loggedin',
          }
          servers.set(payload.serverId, server)
          set({ servers })
          break
        }

        case 'server.nick': {
          const servers = new Map(get().servers)
          const server = servers.get(payload.serverId)
          if (server) {
            servers.set(payload.serverId, { ...server, nick: payload.nick })
            set({ servers })
          }
          break
        }

        case 'server.away': {
          const servers = new Map(get().servers)
          const server = servers.get(payload.serverId)
          if (server) {
            servers.set(payload.serverId, { ...server, away: payload.away })
            set({ servers })
          }
          break
        }

        case 'server.lag': {
          const servers = new Map(get().servers)
          const server = servers.get(payload.serverId)
          if (server) {
            servers.set(payload.serverId, { ...server, lag: payload.lag })
            set({ servers })
          }
          break
        }

        default:
          console.log('Unhandled message type:', type, payload)
      }
    } catch (err) {
      console.error('Error parsing WebSocket message:', err)
    }
  },
}))
