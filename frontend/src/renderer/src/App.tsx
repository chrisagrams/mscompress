import { Button } from './components/ui/button'

function App(): React.JSX.Element {
  const handleClick = (): void => {
    console.log('Button clicked!')
    window.electron.ipcRenderer.send('ping')
  }

  return (
    <div className="flex min-h-screen items-center justify-center">
      <div className="flex flex-col gap-4 items-center">
        <h1 className="text-4xl font-bold">shadcn + Electron</h1>
        <Button variant='outline' onClick={handleClick}>Click Me</Button>
      </div>
    </div>
  )
}

export default App
