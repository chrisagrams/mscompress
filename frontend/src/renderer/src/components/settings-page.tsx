import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card'
import { Input } from './ui/input'
import { Button } from './ui/button'
import { Separator } from './ui/separator'

export function SettingsPage() {
  return (
    <div className="flex flex-1 flex-col gap-6 overflow-auto p-6">
      <div className="flex flex-col gap-2">
        <h2 className="text-2xl font-bold">Settings</h2>
        <p className="text-muted-foreground">Configure application preferences</p>
      </div>

      <div className="flex flex-col gap-6 max-w-2xl">
        <Card>
          <CardHeader>
            <CardTitle>Compression Settings</CardTitle>
            <CardDescription>Default compression configuration</CardDescription>
          </CardHeader>
          <CardContent className="flex flex-col gap-4">
            <div className="flex flex-col gap-2">
              <label className="text-sm font-medium">Compression Level</label>
              <Input type="number" min="1" max="9" defaultValue="6" placeholder="1-9" />
            </div>
            <div className="flex flex-col gap-2">
              <label className="text-sm font-medium">Default Output Directory</label>
              <div className="flex gap-2">
                <Input type="text" placeholder="/path/to/output" className="flex-1" />
                <Button variant="outline">Browse</Button>
              </div>
            </div>
          </CardContent>
        </Card>

        <Card>
          <CardHeader>
            <CardTitle>Application Settings</CardTitle>
            <CardDescription>General application preferences</CardDescription>
          </CardHeader>
          <CardContent className="flex flex-col gap-4">
            <div className="flex flex-col gap-2">
              <label className="text-sm font-medium">Theme</label>
              <Input type="text" defaultValue="System" placeholder="Light, Dark, or System" />
            </div>
            <div className="flex flex-col gap-2">
              <label className="text-sm font-medium">Auto-save Results</label>
              <Input type="checkbox" className="w-5 h-5" />
            </div>
          </CardContent>
        </Card>

        <Separator />

        <div className="flex justify-end gap-2">
          <Button variant="outline">Reset to Defaults</Button>
          <Button>Save Changes</Button>
        </div>
      </div>
    </div>
  )
}
