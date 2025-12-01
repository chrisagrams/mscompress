import { useState } from 'react'
import { SidebarProvider, SidebarTrigger } from './components/ui/sidebar'
import { AppSidebar } from './components/app-sidebar'
import { CompressDecompressPage } from './components/compress-decompress-page'
import { QAPage } from './components/qa-page'
import { SettingsPage } from './components/settings-page'
import { AboutPage } from './components/about-page'
import { FileTable, FileData } from './components/file-table'

function App(): React.JSX.Element {
  // State for managing selected page
  const [selectedPage, setSelectedPage] = useState<string>('Compress/Decompress')
  
  // State for managing selected files
  const [selectedFiles, setSelectedFiles] = useState<FileData[]>([
    // Example files - replace with actual file selection logic
    {
      id: '1',
      name: 'test.mzML',
      size: 2048576,
      path: '/Users/example/documents/test.mzML',
      type: 'mzml',
      dateModified: new Date('2024-11-15'),
    },
    {
      id: '2',
      name: 'test.msz',
      size: 524288,
      path: '/Users/example/images/test.msz',
      type: 'msz',
      dateModified: new Date('2024-11-20'),
    },
  ])

  const handleRemoveFile = (id: string) => {
    setSelectedFiles((prev) => prev.filter((file) => file.id !== id))
  }

  const handleClearAll = () => {
    setSelectedFiles([])
  }
  
  const renderPage = () => {
    switch (selectedPage) {
      case 'Compress/Decompress':
        return <CompressDecompressPage />
      case 'QA':
        return <QAPage />
      case 'Settings':
        return <SettingsPage />
      case 'About':
        return <AboutPage />
      default:
        return <CompressDecompressPage />
    }
  }

  return (
    <SidebarProvider>
      <AppSidebar selectedPage={selectedPage} onPageSelect={setSelectedPage} />
      <main className="flex-1 w-full flex flex-col h-screen">
        <header className="sticky top-0 flex h-16 shrink-0 items-center gap-4 border-b bg-background px-6 z-10">
          <SidebarTrigger />
          <h1 className="text-xl font-semibold">{selectedPage}</h1>
        </header>
        <div className="flex-1 overflow-hidden flex flex-col">
          {renderPage()}
        </div>
        <FileTable
          files={selectedFiles}
          onRemoveFile={handleRemoveFile}
          onClearAll={handleClearAll}
        />
      </main>
    </SidebarProvider>
  )
}

export default App
