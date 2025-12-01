import { Settings, Archive, TestTubes, Database, Cloud, Info} from 'lucide-react'
import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
  SidebarGroup,
  SidebarGroupContent,
  SidebarGroupLabel,
  SidebarHeader,
  SidebarMenu,
  SidebarMenuButton,
  SidebarMenuItem,
} from './ui/sidebar'
import { Badge } from './ui/badge'
import logo from '../assets/msc_logo.svg'

const menuItems = [
  {
    title: 'Compress/Decompress',
    url: '#',
    icon: Archive,
  },
  {
    title: 'QA',
    url: '#',
    icon: TestTubes,
    },
  {
    title: 'Datasets',
    url: '#',
    icon: Database,
  },
  {
    title: 'Upload/Download',
    url: '#',
    icon: Cloud,
  },
  {
    title: 'Settings',
    url: '#',
    icon: Settings,
  },
  {
    title: 'About',
    url: '#',
    icon: Info,
  }
]

interface AppSidebarProps {
  selectedPage: string
  onPageSelect: (page: string) => void
}

export function AppSidebar({ selectedPage, onPageSelect }: AppSidebarProps) {
  return (
    <Sidebar>
      <SidebarHeader>
        <div className="px-4 py-2 flex items-center gap-2">
          <img src={logo} alt="MSCompress Logo"/>
        </div>
      </SidebarHeader>
      <SidebarContent>
        <SidebarGroup>
          <SidebarGroupLabel>Tools</SidebarGroupLabel>
          <SidebarGroupContent>
            <SidebarMenu>
              {menuItems.map((item) => {
                const isLocked = item.title === 'Datasets' || item.title === 'Upload/Download'
                return (
                  <SidebarMenuItem key={item.title}>
                    <SidebarMenuButton 
                      asChild={!isLocked}
                      isActive={selectedPage === item.title}
                      disabled={isLocked}
                    >
                      {isLocked ? (
                        <div className="flex items-center gap-2 opacity-50 cursor-not-allowed">
                          <item.icon />
                          <span>{item.title}</span>
                          <Badge variant="secondary" className="ml-auto">Coming Soon</Badge>
                        </div>
                      ) : (
                        <a 
                          href={item.url}
                          onClick={(e) => {
                            e.preventDefault()
                            onPageSelect(item.title)
                          }}
                        >
                          <item.icon />
                          <span>{item.title}</span>
                        </a>
                      )}
                    </SidebarMenuButton>
                  </SidebarMenuItem>
                )
              })}
            </SidebarMenu>
          </SidebarGroupContent>
        </SidebarGroup>
      </SidebarContent>
      <SidebarFooter>
        <div className="px-4 py-2 text-sm text-muted-foreground">
          v1.0.0
        </div>
      </SidebarFooter>
    </Sidebar>
  )
}
