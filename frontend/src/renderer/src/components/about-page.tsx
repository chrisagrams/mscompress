import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card'
import logo from '../assets/icon.png'

export function AboutPage() {
  return (
    <div className="flex-1 p-6 overflow-auto">
      <div className="max-w-3xl mx-auto space-y-6">
        <Card>
          <CardHeader>
            <div className="flex items-center gap-4">
              <img src={logo} alt="MSCompress Logo" className="w-16 h-16" />
              <div>
                <CardTitle className="text-2xl">MSCompress</CardTitle>
                <CardDescription>Mass Spectrometry Data Compression Tool</CardDescription>
              </div>
            </div>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <h3 className="font-semibold mb-2">Version</h3>
              <p className="text-sm text-muted-foreground">1.0.0</p>
            </div>
            
            <div>
              <h3 className="font-semibold mb-2">About</h3>
              <p className="text-sm text-muted-foreground">
                MSCompress is a powerful tool for compressing and decompressing mass spectrometry data files.
                It supports mzML and MSZ file formats, providing efficient storage solutions for large datasets.
              </p>
            </div>

            <div>
              <h3 className="font-semibold mb-2">Features</h3>
              <ul className="list-disc list-inside text-sm text-muted-foreground space-y-1">
                <li>Compress and decompress mzML files</li>
                <li>Quality assurance testing</li>
                <li>Batch processing support</li>
              </ul>
            </div>

            <div>
              <h3 className="font-semibold mb-2">License</h3>
              <p className="text-sm text-muted-foreground">
                Copyright © 2025. All rights reserved.
              </p>
            </div>
          </CardContent>
        </Card>
      </div>
    </div>
  )
}
