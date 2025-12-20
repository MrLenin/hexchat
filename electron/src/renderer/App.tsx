import React, { useEffect } from 'react'
import { useStore } from './store'
import ServerTree from './components/ServerTree'
import ChatWindow from './components/ChatWindow'
import UserList from './components/UserList'
import './styles/themes/default.css'

declare global {
  interface Window {
    electronAPI: {
      getWsPort: () => Promise<number>
    }
  }
}

function App() {
  const connect = useStore((state) => state.connect)
  const connected = useStore((state) => state.connected)
  const activeSessionId = useStore((state) => state.activeSessionId)

  useEffect(() => {
    // Connect to WebSocket server
    const initConnection = async () => {
      // Default port, can be configured
      const port = window.electronAPI ? await window.electronAPI.getWsPort() : 9867
      connect(port)
    }
    initConnection()
  }, [connect])

  return (
    <div className="app">
      <div className="app-header">
        <span className="app-title">HexChat Electron</span>
        <span className={`connection-status ${connected ? 'connected' : 'disconnected'}`}>
          {connected ? '● Connected' : '○ Disconnected'}
        </span>
      </div>
      <div className="app-content">
        <div className="server-tree-panel">
          <ServerTree />
        </div>
        <div className="chat-panel">
          <ChatWindow sessionId={activeSessionId} />
        </div>
        <div className="userlist-panel">
          <UserList sessionId={activeSessionId} />
        </div>
      </div>
    </div>
  )
}

export default App
