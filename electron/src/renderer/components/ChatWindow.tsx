import React, { useRef, useEffect, useState, KeyboardEvent } from 'react'
import { useStore } from '../store'
import MessageRenderer from './MessageRenderer'

interface ChatWindowProps {
  sessionId: string | null
}

function ChatWindow({ sessionId }: ChatWindowProps) {
  const sendInput = useStore((state) => state.sendInput)
  const sessions = useStore((state) => state.sessions)
  const updateCounter = useStore((state) => state.updateCounter)
  const [inputText, setInputText] = useState('')
  const messagesEndRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)

  // Get session from store
  const session = sessionId ? sessions.get(sessionId) ?? null : null

  // Auto-scroll to bottom when new messages arrive
  useEffect(() => {
    if (messagesEndRef.current) {
      messagesEndRef.current.scrollIntoView({ behavior: 'smooth' })
    }
  }, [updateCounter])

  const handleKeyDown = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter' && inputText.trim() && sessionId) {
      sendInput(sessionId, inputText)
      setInputText('')
    }
  }

  if (!session) {
    return (
      <div className="chat-window empty">
        <div className="no-session-message">
          Select a channel or server to start chatting
        </div>
      </div>
    )
  }

  return (
    <div className="chat-window">
      {session.topicStripped && (
        <div className="topic-bar" title={session.topic}>
          {session.topicStripped}
        </div>
      )}
      <div className="messages-container">
        {session.messages.map((msg, index) => (
          <div key={index} className="message">
            <MessageRenderer text={msg.text} />
          </div>
        ))}
        <div ref={messagesEndRef} />
      </div>
      <div className="input-container">
        <input
          ref={inputRef}
          type="text"
          className="message-input"
          value={inputText}
          onChange={(e) => setInputText(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder="Type a message..."
        />
      </div>
    </div>
  )
}

export default ChatWindow
