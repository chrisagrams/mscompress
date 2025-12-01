import { SidebarProvider, SidebarTrigger } from './components/ui/sidebar'
import { AppSidebar } from './components/app-sidebar'
import { Button } from './components/ui/button'

function App(): React.JSX.Element {
  const handleClick = (): void => {
    console.log('Button clicked!')
    window.electron.ipcRenderer.send('ping')
  }

  return (
    <SidebarProvider>
      <AppSidebar />
      <main className="flex-1 w-full">
        <div className="flex min-h-screen flex-col">
          <header className="sticky top-0 flex h-16 items-center gap-4 border-b bg-background px-6">
            <SidebarTrigger />
            <h1 className="text-xl font-semibold">shadcn + Electron</h1>
          </header>
          <div className="flex flex-1 items-center justify-center p-6">
            <div className="flex flex-col gap-4 items-center">
              <h2 className="text-2xl font-bold">Welcome to your app</h2>
              <Button variant='outline' onClick={handleClick}>Click Me</Button>
            </div>
          </div>
        </div>
      </main>
    </SidebarProvider>
  )
}

export default App
