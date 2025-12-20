import React from 'react'
import { useStore, UserInfo } from '../store'

interface UserListProps {
  sessionId: string | null
}

function UserList({ sessionId }: UserListProps) {
  const sessions = useStore((state) => state.sessions)
  // Subscribe to updateCounter to force re-renders when userlist changes
  useStore((state) => state.updateCounter)

  const session = sessionId ? sessions.get(sessionId) ?? null : null

  if (!session) {
    return <div className="userlist empty" />
  }

  // Sort users: ops first, then halfops, then voice, then regular
  const sortedUsers = Array.from(session.users.values()).sort((a, b) => {
    const getPriority = (user: UserInfo) => {
      if (user.op) return 0
      if (user.hop) return 1
      if (user.voice) return 2
      return 3
    }
    const priorityDiff = getPriority(a) - getPriority(b)
    if (priorityDiff !== 0) return priorityDiff
    return a.nick.toLowerCase().localeCompare(b.nick.toLowerCase())
  })

  const getModePrefix = (user: UserInfo) => {
    if (user.op) return '@'
    if (user.hop) return '%'
    if (user.voice) return '+'
    return ''
  }

  const getModeClass = (user: UserInfo) => {
    if (user.op) return 'user-op'
    if (user.hop) return 'user-hop'
    if (user.voice) return 'user-voice'
    return 'user-regular'
  }

  return (
    <div className="userlist">
      <div className="userlist-header">
        Users ({session.users.size})
      </div>
      <div className="userlist-content">
        {sortedUsers.map((user) => (
          <div
            key={user.nick}
            className={`user-item ${getModeClass(user)} ${user.away ? 'user-away' : ''}`}
            title={user.hostname || undefined}
          >
            <span className="user-prefix">{getModePrefix(user)}</span>
            <span className="user-nick">{user.nick}</span>
          </div>
        ))}
      </div>
    </div>
  )
}

export default UserList
