import { Button } from './ui/button'

export function CompressDecompressPage() {
  const handleClick = () => {
    console.log('Button clicked!')
    window.electron.ipcRenderer.send('ping')
  }

  return (
    <div className="flex flex-1 items-center justify-center p-6">
      <div className="flex flex-col gap-4 items-center">
        <h2 className="text-2xl font-bold">Compress/Decompress Files</h2>
        <p className="text-muted-foreground">Select files to compress or decompress</p>
        <Button variant='outline' onClick={handleClick}>Add Files</Button>
      </div>
    </div>
  )
}
