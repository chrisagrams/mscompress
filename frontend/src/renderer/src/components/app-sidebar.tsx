import { Settings, Archive, TestTubes} from 'lucide-react'
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
    title: 'Settings',
    url: '#',
    icon: Settings,
  },
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
          <SidebarGroupLabel>Application</SidebarGroupLabel>
          <SidebarGroupContent>
            <SidebarMenu>
              {menuItems.map((item) => (
                <SidebarMenuItem key={item.title}>
                  <SidebarMenuButton 
                    asChild
                    isActive={selectedPage === item.title}
                  >
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
                  </SidebarMenuButton>
                </SidebarMenuItem>
              ))}
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
