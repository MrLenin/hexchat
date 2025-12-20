import ReactDOM from 'react-dom/client'
import App from './App'
import './styles/base.css'

// Note: StrictMode removed because it interferes with WebSocket state updates
// in React 18's concurrent rendering mode
ReactDOM.createRoot(document.getElementById('root')!).render(<App />)
