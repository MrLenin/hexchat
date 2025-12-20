import React from 'react'
import { useStore, SessionState } from '../store'

function ServerTree() {
  const { sessions, activeSessionId, setActiveSession } = useStore()

  // Group sessions by server
  const sessionsByServer = new Map<string, SessionState[]>()
  sessions.forEach((session) => {
    const serverId = session.serverId || 'unknown'
    if (!sessionsByServer.has(serverId)) {
      sessionsByServer.set(serverId, [])
    }
    sessionsByServer.get(serverId)!.push(session)
  })

  const getTabColorClass = (color: string) => {
    switch (color) {
      case 'data': return 'tab-data'
      case 'message': return 'tab-message'
      case 'hilight': return 'tab-hilight'
      default: return ''
    }
  }

  const getSessionIcon = (type: string) => {
    switch (type) {
      case 'server': return '🖥️'
      case 'channel': return '#'
      case 'dialog': return '💬'
      default: return '📄'
    }
  }

  return (
    <div className="server-tree">
      {Array.from(sessionsByServer.entries()).map(([serverId, serverSessions]) => (
        <div key={serverId} className="server-group">
          {serverSessions.map((session) => (
            <div
              key={session.sessionId}
              className={`session-item ${session.sessionId === activeSessionId ? 'active' : ''} ${getTabColorClass(session.tabColor)}`}
              onClick={() => setActiveSession(session.sessionId)}
            >
              <span className="session-icon">{getSessionIcon(session.sessionType)}</span>
              <span className="session-name">{session.name || '(unnamed)'}</span>
            </div>
          ))}
        </div>
      ))}
      {sessions.size === 0 && (
        <div className="no-sessions">
          No sessions yet.
          <br />
          Waiting for connection...
        </div>
      )}
    </div>
  )
}

export default ServerTree
