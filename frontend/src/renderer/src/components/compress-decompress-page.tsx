import { Button } from './ui/button'
import { Card } from './ui/card'
import { FileTable, FileData } from './file-table'

// Mock data for staging
const mockStagingFiles: FileData[] = [
  {
    id: '1',
    name: 'sample1.mzML',
    size: 5242880,
    path: '/path/to/sample1.mzML',
    type: 'mzml',
    dateModified: new Date('2024-11-15T10:30:00')
  },
  {
    id: '2',
    name: 'sample2.mzML',
    size: 7340032,
    path: '/path/to/sample2.mzML',
    type: 'mzml',
    dateModified: new Date('2024-11-16T14:20:00')
  },
  {
    id: '3',
    name: 'experiment.mzML',
    size: 12582912,
    path: '/path/to/experiment.mzML',
    type: 'mzml',
    dateModified: new Date('2024-11-17T09:15:00')
  }
]

// Mock data for output
const mockOutputFiles: FileData[] = [
  {
    id: '4',
    name: 'sample1.msz',
    size: 2097152,
    path: '/path/to/output/sample1.msz',
    type: 'msz',
    dateModified: new Date('2024-11-20T11:45:00'),
    progress: 100
  },
  {
    id: '5',
    name: 'sample2.msz',
    size: 2936012,
    path: '/path/to/output/sample2.msz',
    type: 'msz',
    dateModified: new Date('2024-11-20T11:46:00'),
    progress: 65
  },
  {
    id: '6',
    name: 'experiment.msz',
    size: 0,
    path: '/path/to/output/experiment.msz',
    type: 'msz',
    dateModified: new Date('2024-11-20T11:47:00'),
    progress: 0
  }
]

export function CompressDecompressPage() {
  const handleConvert = () => {
    console.log('Convert button clicked!')
  }

  return (
    <div className="flex flex-1 flex-col p-6 gap-4">
      <div className="grid grid-cols-2 gap-4 flex-1">
        {/* Staging Card */}
        <Card className="flex flex-col">
          <div className="p-4 border-b">
            <h3 className="text-lg font-semibold">Queue</h3>
            <p className="text-sm text-muted-foreground">Files ready to process</p>
          </div>
          <div className="flex-1 overflow-auto">
            <FileTable 
              files={mockStagingFiles} 
              columns={['filename', 'size', 'type', 'modified']}
              showHeader={false}
            />
          </div>
        </Card>

        {/* Output Card */}
        <Card className="flex flex-col">
          <div className="p-4 border-b">
            <h3 className="text-lg font-semibold">Output</h3>
            <p className="text-sm text-muted-foreground">Processed files</p>
          </div>
          <div className="flex-1 overflow-auto">
            <FileTable 
              files={mockOutputFiles} 
              columns={['filename', 'type', 'progress']}
              showHeader={false}
            />
          </div>
        </Card>
      </div>

      {/* Convert Button */}
      <div className="flex justify-center pt-2">
        <Button size="lg" onClick={handleConvert}>
          Convert Files
        </Button>
      </div>
    </div>
  )
}
