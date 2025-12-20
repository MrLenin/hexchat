import { app, BrowserWindow, ipcMain } from 'electron'
import { spawn, ChildProcess } from 'child_process'
import path from 'path'

let mainWindow: BrowserWindow | null = null
let backendProcess: ChildProcess | null = null

const WS_PORT = 9867

// Detect if running in development (not packaged)
const isDev = !app.isPackaged

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1200,
    height: 800,
    webPreferences: {
      preload: path.join(__dirname, '../preload/index.js'),
      nodeIntegration: false,
      contextIsolation: true,
    },
    title: 'HexChat Electron',
  })

  // In development, load from Vite dev server
  if (isDev) {
    mainWindow.loadURL('http://localhost:5173')
    mainWindow.webContents.openDevTools()
  } else {
    // In production, load the built files
    mainWindow.loadFile(path.join(__dirname, '../renderer/index.html'))
  }

  mainWindow.on('closed', () => {
    mainWindow = null
  })
}

function startBackend() {
  // Determine backend executable path
  let backendPath: string

  if (isDev) {
    // In development, expect it in the build directory relative to the electron app
    // Adjust this path based on your build setup
    backendPath = path.join(__dirname, '../../../../build/src/fe-electron/hexchat-electron')
    if (process.platform === 'win32') {
      backendPath += '.exe'
    }
  } else {
    // In production, it's in resources/bin
    backendPath = path.join(process.resourcesPath, 'bin', 'hexchat-electron')
    if (process.platform === 'win32') {
      backendPath += '.exe'
    }
  }

  const backendDir = path.dirname(backendPath)
  console.log(`Starting backend: ${backendPath} (cwd: ${backendDir})`)

  try {
    backendProcess = spawn(backendPath, ['--ws-port', WS_PORT.toString()], {
      stdio: ['ignore', 'pipe', 'pipe'],
      cwd: backendDir,
    })

    backendProcess.stdout?.on('data', (data) => {
      console.log(`[Backend] ${data.toString().trim()}`)
    })

    backendProcess.stderr?.on('data', (data) => {
      console.error(`[Backend Error] ${data.toString().trim()}`)
    })

    backendProcess.on('error', (err) => {
      console.error('Failed to start backend:', err)
    })

    backendProcess.on('exit', (code) => {
      console.log(`Backend exited with code ${code}`)
      backendProcess = null
    })
  } catch (err) {
    console.error('Error starting backend:', err)
  }
}

function stopBackend() {
  if (backendProcess) {
    console.log('Stopping backend...')
    backendProcess.kill()
    backendProcess = null
  }
}

// IPC handlers
ipcMain.handle('get-ws-port', () => WS_PORT)

app.whenReady().then(() => {
  startBackend()

  // Give backend time to start
  setTimeout(createWindow, 1000)

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow()
    }
  })
})

app.on('window-all-closed', () => {
  stopBackend()
  if (process.platform !== 'darwin') {
    app.quit()
  }
})

app.on('before-quit', () => {
  stopBackend()
})
