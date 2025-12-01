import { useState } from 'react'
import { ChevronDown, ChevronUp, X } from 'lucide-react'
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

export function FileTable({ files = [], onRemoveFile, onClearAll }: FileTableProps) {
  const [isOpen, setIsOpen] = useState(true)

  if (files.length === 0) {
    return null
  }

  return (
    <div className="w-full border-t bg-background">
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
              Selected Files ({files.length})
            </h3>
          </div>
          {onClearAll && (
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
          <div className="max-h-64 overflow-y-auto">
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead className="w-[40%]">File Name</TableHead>
                  <TableHead className="w-[15%]">Size</TableHead>
                  <TableHead className="w-[15%]">Type</TableHead>
                  <TableHead className="w-[20%]">Modified</TableHead>
                  <TableHead className="w-[10%] text-right">Actions</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {files.map((file) => (
                  <TableRow key={file.id}>
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
        </CollapsibleContent>
      </Collapsible>
    </div>
  )
}
