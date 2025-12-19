import React, { useState, useEffect } from 'react';
import { 
  Cpu, 
  Layers, 
  Network, 
  HardDrive, 
  Code, 
  FileText, 
  Folder, 
  FolderOpen, 
  ChevronRight, 
  Star,
  Play,
  Zap,
  BookOpen,
  Coffee,
  Github,
  Menu,
  X,
  Lightbulb,
  AlertCircle,
  CheckCircle2,
  GraduationCap
} from 'lucide-react';

// --- Components ---

const Badge = ({ children, color = "blue", icon: Icon }) => {
  const colorClasses = {
    blue: "bg-blue-50 text-blue-700 border-blue-200",
    green: "bg-emerald-50 text-emerald-700 border-emerald-200",
    purple: "bg-purple-50 text-purple-700 border-purple-200",
    orange: "bg-orange-50 text-orange-700 border-orange-200",
  };

  return (
    <span className={`inline-flex items-center px-3 py-1 rounded-md text-xs font-medium border ${colorClasses[color]} transition-all hover:shadow-sm cursor-default`}>
      {Icon && <Icon size={12} className="mr-1.5" />}
      {children}
    </span>
  );
};

const SectionTitle = ({ children, subtitle }) => (
  <div className="mb-12 text-center">
    <h2 className="text-3xl font-bold text-slate-900 mb-3 tracking-tight">
      {children}
    </h2>
    <div className="h-1 w-16 bg-blue-500 mx-auto rounded-full opacity-80"></div>
  </div>
);

const TimelineItem = ({ phase, title, tags, link, index }) => (
  <div className="relative pl-8 md:pl-0 md:grid md:grid-cols-5 md:gap-8 group">
    {/* Line */}
    <div className="absolute left-0 top-0 bottom-0 w-px bg-slate-200 md:left-1/2 md:-ml-px group-hover:bg-blue-400 transition-colors"></div>
    
    {/* Dot */}
    <div className="absolute left-[-4px] top-6 w-2.5 h-2.5 rounded-full bg-white border-2 border-slate-400 md:left-1/2 md:-ml-[5px] group-hover:border-blue-600 group-hover:scale-125 transition-all"></div>

    {/* Content */}
    <div className={`mb-10 md:mb-0 md:col-span-2 ${index % 2 === 0 ? 'md:text-right md:pr-8' : 'md:col-start-4 md:pl-8'}`}>
      <div className="bg-white p-6 rounded-xl border border-slate-200 shadow-sm hover:shadow-md hover:border-blue-300 transition-all group-hover:-translate-y-1">
        <div className="flex items-center gap-2 mb-2 md:justify-end justify-start">
           <span className="text-xs font-bold text-blue-600 bg-blue-50 px-2 py-0.5 rounded">{phase}</span>
        </div>
        <h3 className="text-lg font-bold text-slate-800 mb-2">{title}</h3>
        <div className="flex flex-wrap gap-2 mb-4 md:justify-end justify-start">
          {tags.map((tag, i) => (
            <span key={i} className="text-xs text-slate-600 bg-slate-100 px-2 py-1 rounded border border-slate-200">{tag}</span>
          ))}
        </div>
        <a href={link} className="inline-flex items-center text-sm text-blue-600 font-medium hover:text-blue-800 transition-colors hover:underline decoration-blue-300 underline-offset-4">
          查看实现代码 <ChevronRight size={14} className="ml-1" />
        </a>
      </div>
    </div>
    
    {/* Date/Meta */}
    <div className={`hidden md:block md:col-span-2 pt-7 ${index % 2 === 0 ? 'md:col-start-4 md:pl-8 text-left' : 'md:text-right md:pr-8'}`}>
      <div className="text-slate-400 text-sm font-medium">Step {index + 1}</div>
    </div>
  </div>
);

const FileTreeItem = ({ name, type = 'file', children, description, level = 0 }) => {
  const [isOpen, setIsOpen] = useState(false);
  const paddingLeft = `${level * 1.5}rem`;
  
  const toggle = () => setIsOpen(!isOpen);

  return (
    <div className="select-none text-sm">
      <div 
        className={`flex items-center py-1.5 px-4 hover:bg-blue-50 cursor-pointer transition-colors border-l-2 ${isOpen ? 'border-blue-500 bg-blue-50/50' : 'border-transparent'}`}
        style={{ paddingLeft: level === 0 ? '1rem' : paddingLeft }}
        onClick={toggle}
      >
        <span className="mr-2">
          {type === 'folder' ? (
            isOpen ? <FolderOpen size={16} className="text-blue-500" /> : <Folder size={16} className="text-blue-500" />
          ) : (
            <FileText size={16} className="text-slate-400" />
          )}
        </span>
        <span className={`font-mono ${type === 'folder' ? 'text-slate-800 font-semibold' : 'text-slate-600'}`}>
          {name}
        </span>
        {description && (
          <span className="ml-auto text-xs text-slate-400 italic hidden sm:block truncate max-w-[200px]">
            // {description}
          </span>
        )}
      </div>
      
      {isOpen && children && (
        <div className="animate-in slide-in-from-top-1 duration-200">
          {children}
        </div>
      )}
    </div>
  );
};

const NoteCard = ({ title, points, type = "tip" }) => {
  const colors = {
    tip: "bg-amber-50 border-amber-200 text-amber-900",
    info: "bg-blue-50 border-blue-200 text-blue-900",
  };
  const Icon = type === 'tip' ? Lightbulb : AlertCircle;

  return (
    <div className={`p-6 rounded-lg border ${colors[type]} shadow-sm h-full`}>
      <div className="flex items-center gap-2 mb-4 font-bold text-lg">
        <Icon className={type === 'tip' ? "text-amber-500" : "text-blue-500"} />
        {title}
      </div>
      <ul className="space-y-3">
        {points.map((point, idx) => (
          <li key={idx} className="flex gap-3 text-sm leading-relaxed opacity-90">
             <span className="mt-1.5 w-1.5 h-1.5 rounded-full bg-current shrink-0" />
             <span>{point}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

// --- Main App ---

export default function App() {
  const [isScrolled, setIsScrolled] = useState(false);
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);

  useEffect(() => {
    const handleScroll = () => {
      setIsScrolled(window.scrollY > 20);
    };
    window.addEventListener('scroll', handleScroll);
    return () => window.removeEventListener('scroll', handleScroll);
  }, []);

  return (
    <div className="min-h-screen bg-slate-50 text-slate-600 font-sans selection:bg-blue-100 selection:text-blue-900">
      
      {/* Navigation */}
      <nav className={`fixed top-0 w-full z-50 transition-all duration-300 border-b ${isScrolled ? 'bg-white/90 backdrop-blur-md border-slate-200 py-3 shadow-sm' : 'bg-transparent border-transparent py-5'}`}>
        <div className="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8 flex justify-between items-center">
          <div className="flex items-center gap-2 font-bold text-xl text-slate-800 tracking-tight">
            <GraduationCap className="text-blue-600" />
            <span>UCAS<span className="text-blue-600">OS</span> Guide</span>
          </div>
          
          <div className="hidden md:flex items-center gap-8 text-sm font-medium text-slate-600">
            <a href="#roadmap" className="hover:text-blue-600 transition-colors">学习路线</a>
            <a href="#structure" className="hover:text-blue-600 transition-colors">代码架构</a>
            <a href="#tips" className="hover:text-blue-600 transition-colors">避坑指南</a>
            <a href="https://github.com/suiqingying/ucas-os-kernel" target="_blank" rel="noopener noreferrer" className="flex items-center gap-2 bg-slate-900 hover:bg-slate-800 text-white px-4 py-2 rounded-full transition-all shadow-md hover:shadow-lg">
              <Github size={16} />
              <span>GitHub</span>
            </a>
          </div>

          <button className="md:hidden text-slate-800" onClick={() => setMobileMenuOpen(!mobileMenuOpen)}>
            {mobileMenuOpen ? <X /> : <Menu />}
          </button>
        </div>
      </nav>
      
      {/* Mobile Menu */}
      {mobileMenuOpen && (
         <div className="fixed inset-0 z-40 bg-white pt-24 px-6 md:hidden">
            <div className="flex flex-col gap-6 text-lg font-medium text-slate-800">
              <a href="#roadmap" onClick={() => setMobileMenuOpen(false)}>学习路线</a>
              <a href="#structure" onClick={() => setMobileMenuOpen(false)}>代码架构</a>
              <a href="#tips" onClick={() => setMobileMenuOpen(false)}>避坑指南</a>
            </div>
         </div>
      )}

      {/* Hero Section */}
      <section className="relative pt-32 pb-20 md:pt-48 md:pb-24 overflow-hidden">
         {/* Simple background pattern */}
         <div className="absolute inset-0 bg-[linear-gradient(to_right,#80808012_1px,transparent_1px),linear-gradient(to_bottom,#80808012_1px,transparent_1px)] bg-[size:24px_24px] [mask-image:radial-gradient(ellipse_60%_50%_at_50%_0%,#000_70%,transparent_100%)]"></div>

        <div className="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8 text-center relative z-10">
          <div className="inline-flex items-center gap-2 px-4 py-1.5 rounded-full bg-blue-50 border border-blue-100 text-blue-600 text-sm font-medium mb-8 animate-fade-in-up">
            <CheckCircle2 size={16} />
            UCAS 操作系统研讨课参考代码
          </div>
          
          <h1 className="text-4xl md:text-6xl font-extrabold text-slate-900 tracking-tight mb-6 animate-fade-in-up delay-100">
            给下一届学弟学妹的 <br className="hidden md:block" />
            <span className="text-blue-600">操作系统 (OS Kernel)</span> 实现笔记
          </h1>
          
          <p className="max-w-2xl mx-auto text-lg text-slate-500 mb-10 leading-relaxed animate-fade-in-up delay-200">
            这份仓库记录了我从零构建 RISC-V 内核的全过程。无论你是正在被 Bootloader 卡住，还是在调试 Page Fault，希望这里的代码和笔记能给你一些灵感。
          </p>
          
          <div className="flex flex-wrap justify-center gap-3 mb-12 animate-fade-in-up delay-300">
            <Badge color="blue" icon={Cpu}>架构: RISC-V</Badge>
            <Badge color="purple" icon={Code}>语言: C / Assembly</Badge>
            <Badge color="green" icon={Zap}>状态: 已完结</Badge>
          </div>

          <div className="flex flex-col sm:flex-row justify-center gap-4 animate-fade-in-up delay-300">
            <a href="#roadmap" className="px-8 py-3 bg-blue-600 hover:bg-blue-700 text-white rounded-lg font-medium transition-all shadow-lg shadow-blue-200 flex items-center justify-center gap-2">
              <BookOpen size={18} />
              查看学习路线
            </a>
            <a href="https://github.com/suiqingying/ucas-os-kernel" className="px-8 py-3 bg-white hover:bg-slate-50 text-slate-700 border border-slate-200 rounded-lg font-medium transition-all shadow-sm hover:shadow-md flex items-center justify-center gap-2">
              <Star size={18} className="text-amber-400" />
              收藏仓库
            </a>
          </div>
        </div>
      </section>

      {/* Roadmap Section */}
      <section id="roadmap" className="py-20 bg-white">
        <div className="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8">
          <SectionTitle subtitle>通关路线图</SectionTitle>
          <p className="text-center text-slate-500 max-w-2xl mx-auto mb-16">
            我是按照以下顺序完成实验的。建议先搞定环境，再逐步深入内核核心。点击卡片可以直接跳转到对应分支的代码。
          </p>
          
          <div className="relative">
            <div className="space-y-12">
              <TimelineItem 
                phase="Prj0" 
                title="环境准备"
                tags={['QEMU 配置', 'GDB 调试', 'OpenSBI']}
                link="https://github.com/suiqingying/ucas-os-kernel/tree/main/guide"
                index={0}
              />
              <TimelineItem 
                phase="Prj1" 
                title="引导与加载 (Bootloader)"
                tags={['汇编启动代码', 'ELF 解析', '内核入口']}
                link="https://github.com/suiqingying/ucas-os-kernel/tree/Prj1"
                index={1}
              />
              <TimelineItem 
                phase="Prj2" 
                title="内核核心机制"
                tags={['上下文切换', '时钟中断', '锁机制']}
                link="https://github.com/suiqingying/ucas-os-kernel/tree/Prj2"
                index={2}
              />
              <TimelineItem 
                phase="Prj3" 
                title="进程与调度"
                tags={['PCB 结构', '调度算法', '优先级']}
                link="https://github.com/suiqingying/ucas-os-kernel/tree/Prj3"
                index={3}
              />
              <TimelineItem 
                phase="Prj4" 
                title="虚拟内存管理"
                tags={['Sv39 页表', '缺页异常', 'TLB 刷新']}
                link="https://github.com/suiqingying/ucas-os-kernel/tree/Prj4"
                index={4}
              />
              <TimelineItem 
                phase="Prj5" 
                title="网络驱动与协议栈"
                tags={['E1000 网卡驱动', 'DMA 描述符', 'TCP/IP']}
                link="https://github.com/suiqingying/ucas-os-kernel/tree/Prj5"
                index={5}
              />
            </div>
          </div>
        </div>
      </section>

      {/* Code Structure Section */}
      <section id="structure" className="py-24 bg-slate-50 border-y border-slate-200">
        <div className="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8">
           <div className="grid md:grid-cols-2 gap-16 items-start">
             <div>
                <div className="sticky top-24">
                  <h2 className="text-3xl font-bold text-slate-900 mb-6">全景代码结构</h2>
                  <p className="text-slate-600 mb-8 leading-relaxed">
                    这是 <code>Prj5</code> 版本的目录结构。为了方便阅读，我将内核分为核心层、驱动层和架构相关层。右侧是完整的文件树预览。
                  </p>
                  
                  <div className="space-y-6">
                    <div className="flex gap-4 p-4 bg-white rounded-xl border border-slate-200 shadow-sm">
                      <div className="mt-1 bg-blue-100 p-2.5 rounded-lg text-blue-600 h-fit">
                        <Play size={20} />
                      </div>
                      <div>
                        <h4 className="text-slate-800 font-bold mb-1">系统启动入口 (init)</h4>
                        <p className="text-sm text-slate-500">
                          一切的起点。<code>main.c</code> 负责初始化所有子系统，是理解整个 OS 启动流程的最佳入口。
                        </p>
                      </div>
                    </div>

                    <div className="flex gap-4 p-4 bg-white rounded-xl border border-slate-200 shadow-sm">
                      <div className="mt-1 bg-purple-100 p-2.5 rounded-lg text-purple-600 h-fit">
                        <Layers size={20} />
                      </div>
                      <div>
                        <h4 className="text-slate-800 font-bold mb-1">核心子系统 (kernel)</h4>
                        <p className="text-sm text-slate-500">
                          包含 <code>sched</code> (调度)、<code>mm</code> (内存)、<code>syscall</code> (系统调用) 等核心逻辑。这是操作系统的灵魂。
                        </p>
                      </div>
                    </div>

                    <div className="flex gap-4 p-4 bg-white rounded-xl border border-slate-200 shadow-sm">
                      <div className="mt-1 bg-emerald-100 p-2.5 rounded-lg text-emerald-600 h-fit">
                        <Network size={20} />
                      </div>
                      <div>
                        <h4 className="text-slate-800 font-bold mb-1">网络与驱动 (drivers)</h4>
                        <p className="text-sm text-slate-500">
                          <code>Prj5</code> 的重头戏。E1000 网卡驱动实现非常硬核，涉及 DMA 环形缓冲区的管理。
                        </p>
                      </div>
                    </div>
                  </div>
                </div>
             </div>

             {/* Interactive File Tree - Light Mode */}
             <div className="bg-white rounded-xl border border-slate-200 overflow-hidden shadow-lg shadow-slate-200/50">
               <div className="bg-slate-50 px-4 py-3 border-b border-slate-200 flex justify-between items-center">
                 <span className="text-sm text-slate-600 font-mono font-medium flex items-center gap-2">
                   <Folder size={14} className="text-blue-500"/> ucas-os-kernel /
                 </span>
                 <div className="flex gap-1.5 opacity-60">
                   <div className="w-2.5 h-2.5 rounded-full bg-slate-300"></div>
                   <div className="w-2.5 h-2.5 rounded-full bg-slate-300"></div>
                   <div className="w-2.5 h-2.5 rounded-full bg-slate-300"></div>
                 </div>
               </div>
               <div className="p-2 overflow-y-auto max-h-[600px] bg-white">
                 <FileTreeItem name="init" type="folder" description="System Boot Entry">
                    <FileTreeItem name="main.c" type="file" description="Kernel Entry Point" level={1} />
                 </FileTreeItem>

                 <FileTreeItem name="arch" type="folder" description="RISC-V Specifics">
                    <FileTreeItem name="riscv" type="folder" level={1}>
                      <FileTreeItem name="boot" type="folder" level={2}>
                         <FileTreeItem name="bootblock.S" type="file" description="The Loader" level={3} />
                      </FileTreeItem>
                      <FileTreeItem name="kernel" type="folder" level={2}>
                         <FileTreeItem name="entry.S" type="file" description="Trap Entry" level={3} />
                         <FileTreeItem name="head.S" type="file" level={3} />
                      </FileTreeItem>
                      <FileTreeItem name="include" type="folder" level={2} />
                    </FileTreeItem>
                 </FileTreeItem>

                 <FileTreeItem name="kernel" type="folder" description="Core Subsystems">
                    <FileTreeItem name="sched" type="folder" description="Scheduler" level={1} />
                    <FileTreeItem name="syscall" type="folder" description="System Calls" level={1} />
                    <FileTreeItem name="mm" type="folder" description="Memory Manager" level={1} />
                    <FileTreeItem name="net" type="folder" description="TCP/IP Stack" level={1} />
                    <FileTreeItem name="locking" type="folder" level={1} />
                    <FileTreeItem name="irq" type="folder" level={1} />
                 </FileTreeItem>

                 <FileTreeItem name="drivers" type="folder" description="Device Drivers">
                    <FileTreeItem name="e1000.c" type="file" description="Network Card" level={1} />
                    <FileTreeItem name="plic.c" type="file" description="Interrupt Controller" level={1} />
                    <FileTreeItem name="screen.c" type="file" description="VGA Output" level={1} />
                 </FileTreeItem>

                 <FileTreeItem name="libs" type="folder" description="Kernel Utilities">
                    <FileTreeItem name="printk.c" type="file" description="Kernel printf" level={1} />
                    <FileTreeItem name="string.c" type="file" level={1} />
                 </FileTreeItem>

                 <FileTreeItem name="test" type="folder" description="Test Cases">
                    <FileTreeItem name="test_project5" type="folder" level={1} />
                 </FileTreeItem>
               </div>
             </div>
           </div>
        </div>
      </section>

      {/* Advice Section */}
      <section id="tips" className="py-20 bg-white">
        <div className="max-w-6xl mx-auto px-4 sm:px-6 lg:px-8">
           <SectionTitle subtitle>学长/学姐的碎碎念</SectionTitle>
           <p className="text-center text-slate-500 mb-12">做系统实验心态最重要。这里有几条用发量换来的建议：</p>

           <div className="grid md:grid-cols-2 gap-8">
             <NoteCard 
               title="调试技巧 (Debug)" 
               type="tip"
               points={[
                 "printk 是真正的神器。不要过分依赖 GDB，在关键路径打 log 往往能更快定位问题。",
                 "多打印十六进制地址。很多 Page Fault 都是因为指针偏移算错了，或者基地址配错了。",
                 "遇到死机先看内核栈。栈帧信息是你最后的救命稻草。"
               ]}
             />
             <NoteCard 
               title="学习心态 (Mindset)" 
               type="info"
               points={[
                 "不要直接 Copy 代码。理解“为什么”比写出代码更重要，否则答辩时会被问倒。",
                 "理解上下文切换的本质。搞清楚为什么切换时要保存这些寄存器，是 Prj2 和 Prj3 的核心。",
                 "心态要稳。环境配半天、Bootblock 跑不起来都是常态，解决 Bug 后的成就感是无与伦比的。"
               ]}
             />
           </div>

           <div className="mt-16 p-8 bg-blue-50 rounded-2xl border border-blue-100 flex flex-col md:flex-row items-center justify-between gap-6">
             <div className="flex items-center gap-4">
               <div className="p-3 bg-blue-100 rounded-full text-blue-600">
                 <Coffee size={32} />
               </div>
               <div>
                 <h3 className="text-xl font-bold text-slate-800">准备好开始了吗？</h3>
                 <p className="text-slate-600">从 <code>Prj1</code> 分支开始，一步步构建你的内核吧。</p>
               </div>
             </div>
             <a href="https://github.com/suiqingying/ucas-os-kernel/tree/Prj1" className="px-6 py-3 bg-blue-600 hover:bg-blue-700 text-white rounded-lg font-medium transition-colors shadow-sm">
               前往 Prj1 分支
             </a>
           </div>
        </div>
      </section>

      {/* Footer */}
      <footer className="py-12 bg-slate-50 border-t border-slate-200 text-center">
         <div className="flex items-center justify-center gap-2 mb-4 text-slate-800 font-bold text-lg">
           <Cpu size={20} className="text-blue-600"/> UCAS OS Kernel
         </div>
         <p className="text-slate-500 text-sm mb-6 max-w-md mx-auto">
           希望这份笔记能成为你 OS 学习路上的垫脚石，而不是绊脚石。<br/>
           Good Luck & Have Fun!
         </p>
         <div className="flex justify-center gap-8 text-sm font-medium text-slate-600">
            <a href="https://github.com/suiqingying/ucas-os-kernel" className="hover:text-blue-600 transition-colors">GitHub 仓库</a>
            <a href="https://github.com/suiqingying/ucas-os-kernel/issues" className="hover:text-blue-600 transition-colors">提出 Issue</a>
         </div>
      </footer>
    </div>
  );
}