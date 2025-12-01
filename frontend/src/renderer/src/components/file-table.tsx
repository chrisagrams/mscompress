import { useState, useEffect } from 'react'
import { ChevronDown, ChevronUp, X, Upload } from 'lucide-react'
import {
  Collapsible,
  CollapsibleContent,
  CollapsibleTrigger,
} from './ui/collapsible'
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from './ui/table'
import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Checkbox } from './ui/checkbox'

export interface FileData {
  id: string
  name: string
  size: number
  path: string
  type: 'msz' | 'mzml'
  dateModified?: Date
}

interface FileTableProps {
  files?: FileData[]
  onRemoveFile?: (id: string) => void
  onClearAll?: () => void
  onFilesDropped?: (files: File[]) => void
}

function formatFileSize(bytes: number): string {
  if (bytes === 0) return '0 Bytes'
  const k = 1024
  const sizes = ['Bytes', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return Math.round((bytes / Math.pow(k, i)) * 100) / 100 + ' ' + sizes[i]
}

function formatDate(date?: Date): string {
  if (!date) return '-'
  return new Intl.DateTimeFormat('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
  }).format(date)
}

export function FileTable({ files = [], onRemoveFile, onClearAll, onFilesDropped }: FileTableProps) {
  const [isOpen, setIsOpen] = useState(true)
  const [isDragging, setIsDragging] = useState(false)
  const [selectedFiles, setSelectedFiles] = useState<Set<string>>(new Set())

  // Initialize all files as selected when files change
  useEffect(() => {
    setSelectedFiles(new Set(files.map(file => file.id)))
  }, [files])

  const handleDragEnter = (e: React.DragEvent) => {
    e.preventDefault()
    e.stopPropagation()
    setIsDragging(true)
  }

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault()
    e.stopPropagation()
    // Only set dragging to false if we're leaving the container itself
    if (e.currentTarget === e.target) {
      setIsDragging(false)
    }
  }

  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault()
    e.stopPropagation()
  }

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault()
    e.stopPropagation()
    setIsDragging(false)

    if (onFilesDropped && e.dataTransfer.files) {
      const droppedFiles = Array.from(e.dataTransfer.files)
      onFilesDropped(droppedFiles)
    }
  }

  return (
    <div 
      className={`relative w-full border-t transition-all duration-200 ${
        isDragging 
          ? 'bg-primary/10' 
          : 'bg-background'
      }`}
      onDragEnter={handleDragEnter}
      onDragLeave={handleDragLeave}
      onDragOver={handleDragOver}
      onDrop={handleDrop}
    >
      {isDragging && (
        <div className="absolute inset-0 z-10 flex items-center justify-center bg-primary/5 backdrop-blur-[2px] pointer-events-none">
          <div className="flex flex-col items-center gap-3 p-6 rounded-lg bg-background/90 border-2 border-dashed border-primary shadow-lg">
            <Upload className="h-12 w-12 text-primary animate-bounce" />
            <p className="text-lg font-semibold text-primary">Drop files here</p>
            <p className="text-sm text-muted-foreground">Release to add files to the table</p>
          </div>
        </div>
      )}
      <Collapsible open={isOpen} onOpenChange={setIsOpen}>
        <div className="flex items-center justify-between px-6 py-3 border-b">
          <div className="flex items-center gap-2">
            <CollapsibleTrigger asChild>
              <Button variant="ghost" size="sm" className="p-0 h-auto">
                {isOpen ? (
                  <ChevronDown className="h-4 w-4" />
                ) : (
                  <ChevronUp className="h-4 w-4" />
                )}
              </Button>
            </CollapsibleTrigger>
            <h3 className="font-semibold text-sm">
              File Manager {files.length > 0 && `(${selectedFiles.size}/${files.length})`}
            </h3>
          </div>
          {onClearAll && files.length > 0 && (
            <Button
              variant="ghost"
              size="sm"
              onClick={onClearAll}
              className="text-xs"
            >
              Clear All
            </Button>
          )}
        </div>
        <CollapsibleContent>
          {files.length === 0 ? (
            <div className="flex flex-col items-center justify-center py-12 px-6 text-center">
              <Upload className="h-12 w-12 text-muted-foreground/50 mb-4" />
              <h4 className="font-semibold text-sm mb-2">No files selected</h4>
              <p className="text-sm text-muted-foreground max-w-md">
                Drag and drop files here or use the file selection button above to add MSZ or MZML files for processing
              </p>
            </div>
          ) : (
            <div className="max-h-64 overflow-y-auto">
              <Table>
              <TableHeader>
                <TableRow>
                  <TableHead className="w-[5%]"></TableHead>
                  <TableHead className="w-[35%]">File Name</TableHead>
                  <TableHead className="w-[15%]">Size</TableHead>
                  <TableHead className="w-[15%]">Type</TableHead>
                  <TableHead className="w-[20%]">Modified</TableHead>
                  <TableHead className="w-[10%] text-right">Actions</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {files.map((file) => (
                  <TableRow key={file.id}>
                    <TableCell>
                      <Checkbox
                        checked={selectedFiles.has(file.id)}
                        onCheckedChange={(checked) => {
                          const newSelected = new Set(selectedFiles)
                          if (checked) {
                            newSelected.add(file.id)
                          } else {
                            newSelected.delete(file.id)
                          }
                          setSelectedFiles(newSelected)
                        }}
                      />
                    </TableCell>
                    <TableCell className="font-medium truncate" title={file.path}>
                      {file.name}
                    </TableCell>
                    <TableCell>{formatFileSize(file.size)}</TableCell>
                    <TableCell>
                      <Badge
                        className="uppercase"
                        style={{
                          backgroundColor: file.type === 'msz' ? 'var(--mscompress-purple)' : 'var(--mscompress-blue)',
                          color: 'white',
                          borderColor: file.type === 'msz' ? 'var(--mscompress-purple)' : 'var(--mscompress-blue)'
                        }}
                      >
                        {file.type}
                      </Badge>
                    </TableCell>
                    <TableCell>{formatDate(file.dateModified)}</TableCell>
                    <TableCell className="text-right">
                      {onRemoveFile && (
                        <Button
                          variant="ghost"
                          size="sm"
                          onClick={() => onRemoveFile(file.id)}
                          className="h-8 w-8 p-0"
                        >
                          <X className="h-4 w-4" />
                        </Button>
                      )}
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </div>
          )}
        </CollapsibleContent>
      </Collapsible>
    </div>
  )
}
