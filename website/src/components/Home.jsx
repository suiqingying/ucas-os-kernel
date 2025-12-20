import React, { useState, useEffect } from 'react';
import { Link } from 'react-router-dom';
import { 
  Cpu, Layers, Network, Code, FileText, Folder, FolderOpen, 
  ChevronRight, Play, Zap, BookOpen, Coffee, Github, CheckCircle2,
  GraduationCap, Star
} from 'lucide-react';

const TimelineItem = ({ phase, title, tags, link, index }) => (
  <div className="relative pl-8 md:pl-0 md:grid md:grid-cols-5 md:gap-8 group">
    {/* Line */}
    <div className="absolute left-0 top-0 bottom-0 w-px bg-[#D8DEE9] md:left-1/2 md:-ml-px group-hover:bg-[#5E81AC] transition-colors"></div>
    
    {/* Dot */}
    <div className="absolute left-[-4px] top-6 w-2.5 h-2.5 rounded-full bg-[#ECF0F1] border-2 border-[#D8DEE9] md:left-1/2 md:-ml-[5px] group-hover:border-[#5E81AC] group-hover:scale-125 transition-all"></div>

    {/* Content */}
    <div className={`mb-10 md:mb-0 md:col-span-2 ${index % 2 === 0 ? 'md:text-right md:pr-8' : 'md:col-start-4 md:pl-8'}`}>
      <Link to={link} className="block bg-white p-6 rounded-xl border border-[#D8DEE9] shadow-sm hover:shadow-md hover:border-[#81A1C1] transition-all group-hover:-translate-y-1">
        <div className="flex items-center gap-2 mb-2 md:justify-end justify-start">
           <span className="text-xs font-bold text-[#5E81AC] bg-[#ECF0F1] px-2 py-0.5 rounded border border-[#D8DEE9] tracking-wide">{phase}</span>
        </div>
        <h3 className="text-lg font-bold text-[#2E3440] mb-2 font-sans tracking-tight">{title}</h3>
        <div className="flex flex-wrap gap-2 mb-4 md:justify-end justify-start">
          {tags.map((tag, i) => (
            <span key={i} className="text-xs text-[#4C566A] bg-[#ECF0F1] px-2 py-1 rounded border border-[#D8DEE9] font-mono">{tag}</span>
          ))}
        </div>
        <div className="inline-flex items-center text-sm text-[#5E81AC] font-medium hover:text-[#81A1C1] transition-colors">
          阅读章节 <ChevronRight size={14} className="ml-1" />
        </div>
      </Link>
    </div>
    
    {/* Date/Meta */}
    <div className={`hidden md:block md:col-span-2 pt-7 ${index % 2 === 0 ? 'md:col-start-4 md:pl-8 text-left' : 'md:text-right md:pr-8'}`}>
      <div className="text-[#4C566A] text-sm font-medium font-mono opacity-80">Chapter {index}</div>
    </div>
  </div>
);

const FileTreeItem = ({ name, type = 'file', children, description, level = 0 }) => {
  const [isOpen, setIsOpen] = useState(false);
  const paddingLeft = `${level * 1.5}rem`;
  const toggle = () => setIsOpen(!isOpen);

  return (
    <div className="select-none text-sm font-mono text-[#4C566A]">
      <div 
        className={`flex items-center py-1.5 px-4 hover:bg-[#E5E9F0] cursor-pointer transition-colors border-l-2 ${isOpen ? 'border-[#5E81AC] bg-[#ECF0F1]' : 'border-transparent'}`}
        style={{ paddingLeft: level === 0 ? '1rem' : paddingLeft }}
        onClick={toggle}
      >
        <span className="mr-2">
          {type === 'folder' ? (
            isOpen ? <FolderOpen size={16} className="text-[#5E81AC]" /> : <Folder size={16} className="text-[#5E81AC]" />
          ) : (
            <FileText size={16} className="text-[#D8DEE9]" />
          )}
        </span>
        <span className={` ${type === 'folder' ? 'text-[#2E3440] font-bold' : 'text-[#4C566A]'}`}>
          {name}
        </span>
        {description && (
          <span className="ml-auto text-xs text-[#B48EAD] italic hidden sm:block truncate max-w-[200px] font-sans">
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

const NoteCard = ({ title, points }) => (
  <div className="p-6 rounded-lg bg-white border border-[#D8DEE9] shadow-sm h-full hover:border-[#81A1C1] transition-colors">
    <div className="flex items-center gap-2 mb-4 font-bold text-lg font-sans text-[#2E3440]">
      <Coffee className="text-[#B48EAD]" size={20} />
      {title}
    </div>
    <ul className="space-y-3">
      {points.map((point, idx) => (
        <li key={idx} className="flex gap-3 text-sm leading-relaxed text-[#4C566A]">
           <span className="mt-2 w-1.5 h-1.5 rounded-full bg-[#A3BE8C] shrink-0" />
           <span>{point}</span>
        </li>
      ))}
    </ul>
  </div>
);

export default function Home() {
  const scrollToSection = (id, e) => {
    e.preventDefault();
    const element = document.getElementById(id);
    if (element) {
      const headerOffset = 80;
      const elementPosition = element.getBoundingClientRect().top;
      const offsetPosition = elementPosition + window.pageYOffset - headerOffset;

      window.scrollTo({
        top: offsetPosition,
        behavior: "smooth"
      });
      window.history.pushState(null, null, `#${id}`);
    }
  };

  return (
    <div className="min-h-screen bg-[#ECF0F1] text-[#2E3440] font-sans">
      {/* Header */}
      <header className="bg-[#ECF0F1]/95 backdrop-blur border-b border-[#D8DEE9] sticky top-0 z-50">
        <div className="max-w-6xl mx-auto px-6 h-16 flex items-center justify-between">
          <Link to="/" className="flex items-center gap-2 font-bold text-xl text-[#2E3440] tracking-tight hover:opacity-80 transition-opacity">
            <GraduationCap className="text-[#5E81AC]" />
            <span>UCAS<span className="text-[#5E81AC]">OS</span> Guide</span>
          </Link>
          <div className="hidden md:flex items-center gap-8 text-sm font-medium text-[#4C566A]">
            <a href="#roadmap" onClick={(e) => scrollToSection('roadmap', e)} className="hover:text-[#5E81AC] transition-colors cursor-pointer">学习路线</a>
            <a href="#structure" onClick={(e) => scrollToSection('structure', e)} className="hover:text-[#5E81ac] transition-colors cursor-pointer">代码架构</a>
            <a href="#tips" onClick={(e) => scrollToSection('tips', e)} className="hover:text-[#5E81ac] transition-colors cursor-pointer">避坑指南</a>
            <a href="https://github.com/suiqingying/ucas-os-kernel" target="_blank" rel="noreferrer" className="flex items-center gap-2 bg-[#2E3440] hover:bg-[#3B4252] text-white px-4 py-2 rounded-md transition-all shadow-sm font-mono text-xs tracking-wide">
              <Github size={16} />
              <span>GitHub</span>
            </a>
          </div>
        </div>
      </header>

      {/* Hero */}
      <section className="relative pt-24 pb-20 overflow-hidden text-center px-4">
         <div className="inline-flex items-center gap-2 px-4 py-1.5 rounded-full bg-[#E5E9F0] border border-[#D8DEE9] text-[#5E81AC] text-sm font-bold mb-8 tracking-wide font-mono">
            <CheckCircle2 size={16} />
            UCAS OS Kernel Reference
          </div>
          
          <h1 className="text-4xl md:text-6xl font-bold text-[#2E3440] tracking-tight mb-6">
            给下一届学弟学妹的 <br className="hidden md:block" />
            <span className="text-[#5E81AC]">操作系统</span> 实现笔记
          </h1>
          
          <p className="max-w-2xl mx-auto text-lg text-[#4C566A] mb-10 leading-relaxed font-mono">
            "The best way to learn an operating system is to write one."
          </p>

          <div className="flex flex-col sm:flex-row justify-center gap-4">
             <button onClick={(e) => scrollToSection('roadmap', e)} className="px-8 py-3 bg-[#5E81AC] hover:bg-[#81A1C1] text-white rounded-md font-bold shadow-sm hover:shadow-md transition-all flex items-center justify-center gap-2 cursor-pointer border border-transparent">
               <BookOpen size={18} /> 开始阅读
             </button>
             <a href="https://github.com/suiqingying/ucas-os-kernel" target="_blank" rel="noreferrer" className="px-8 py-3 bg-white hover:bg-[#E5E9F0] text-[#2E3440] border border-[#D8DEE9] rounded-md font-bold shadow-sm hover:shadow-md transition-all flex items-center justify-center gap-2">
               <Star size={18} className="text-[#BF616A]" /> 给仓库点个 Star
             </a>
          </div>
      </section>

      {/* Roadmap */}
      <section id="roadmap" className="py-20 bg-white border-y border-[#D8DEE9]">
        <div className="max-w-5xl mx-auto px-6">
          <div className="text-center mb-16">
            <h2 className="text-3xl font-bold text-[#2E3440] mb-4">通关路线图</h2>
            <div className="h-1 w-16 bg-[#5E81AC] mx-auto rounded-full opacity-80"></div>
          </div>
          
          <div className="relative">
             <div className="space-y-12">
              <TimelineItem phase="Prj0" title="环境准备" tags={['QEMU', 'GDB']} link={`/read/page-00`} index={0} />
              <TimelineItem phase="Prj1" title="引导与加载" tags={['Bootloader', 'ELF']} link={`/read/page-01`} index={1} />
              <TimelineItem phase="Prj2" title="内核核心机制" tags={['Context Switch', 'Trap']} link={`/read/page-02`} index={2} />
              <TimelineItem phase="Prj3" title="进程与调度" tags={['PCB', 'Scheduler']} link={`/read/page-03`} index={3} />
              <TimelineItem phase="Prj4" title="虚拟内存" tags={['Sv39', 'Page Table']} link={`/read/page-04`} index={4} />
              <TimelineItem phase="Prj5" title="网络驱动" tags={['E1000', 'TCP/IP']} link={`/read/page-05`} index={5} />
             </div>
          </div>
        </div>
      </section>

      {/* Structure */}
      <section id="structure" className="py-20 bg-[#ECF0F1]">
         <div className="max-w-6xl mx-auto px-6 grid md:grid-cols-2 gap-16 items-start">
             <div className="sticky top-24">
                <h2 className="text-3xl font-bold text-[#2E3440] mb-6">代码架构全景</h2>
                <p className="text-[#4C566A] mb-8 leading-relaxed">
                  为了方便阅读，我将 Prj5 版本的内核分为核心层、驱动层和架构相关层。合理的目录结构是减少 Bug 的第一步。
                </p>
                <div className="space-y-4">
                   <div className="p-4 bg-white rounded-lg border border-[#D8DEE9] shadow-sm flex gap-4">
                      <div className="mt-1 text-[#BF616A]"><Play size={20}/></div>
                      <div>
                        <h4 className="font-bold text-[#2E3440]">init (启动)</h4>
                        <p className="text-sm text-[#4C566A]">操作系统的入口，main.c 所在地。</p>
                      </div>
                   </div>
                   <div className="p-4 bg-white rounded-lg border border-[#D8DEE9] shadow-sm flex gap-4">
                      <div className="mt-1 text-[#5E81AC]"><Layers size={20}/></div>
                      <div>
                        <h4 className="font-bold text-[#2E3440]">kernel (核心)</h4>
                        <p className="text-sm text-[#4C566A]">调度 (sched), 内存 (mm), 系统调用 (syscall)。</p>
                      </div>
                   </div>
                </div>
             </div>

             <div className="bg-white rounded-lg border border-[#D8DEE9] shadow-sm p-2 overflow-hidden">
                <div className="p-2 border-b border-[#E5E9F0] mb-2 flex gap-1.5 opacity-50 px-4">
                   <div className="w-2.5 h-2.5 rounded-full bg-[#D8DEE9]"></div>
                   <div className="w-2.5 h-2.5 rounded-full bg-[#D8DEE9]"></div>
                   <div className="w-2.5 h-2.5 rounded-full bg-[#D8DEE9]"></div>
                </div>
                <div className="overflow-y-auto max-h-[500px] custom-scrollbar">
                   <FileTreeItem name="ucas-os-kernel" type="folder">
                      <FileTreeItem name="init" type="folder" level={1}><FileTreeItem name="main.c" level={2}/></FileTreeItem>
                      <FileTreeItem name="arch/riscv" type="folder" level={1} description="Arch Dependent">
                          <FileTreeItem name="boot" type="folder" level={2} />
                          <FileTreeItem name="kernel" type="folder" level={2} />
                      </FileTreeItem>
                      <FileTreeItem name="kernel" type="folder" level={1} description="Core Logic">
                          <FileTreeItem name="sched" type="folder" level={2} />
                          <FileTreeItem name="mm" type="folder" level={2} />
                          <FileTreeItem name="syscall" type="folder" level={2} />
                      </FileTreeItem>
                      <FileTreeItem name="drivers" type="folder" level={1}>
                          <FileTreeItem name="e1000.c" level={2} description="Network Driver" />
                      </FileTreeItem>
                   </FileTreeItem>
                </div>
             </div>
         </div>
      </section>

      {/* Advice */}
      <section id="tips" className="py-20 bg-white border-t border-[#D8DEE9]">
         <div className="max-w-5xl mx-auto px-6">
            <div className="text-center mb-16">
               <h2 className="text-3xl font-bold text-[#2E3440] mb-4">学长的碎碎念</h2>
               <div className="h-1 w-16 bg-[#A3BE8C] mx-auto rounded-full opacity-80"></div>
            </div>
            
            <div className="grid md:grid-cols-2 gap-8">
               <NoteCard title="调试技巧" points={[
                 "printk 是神器，多打 log 比 gdb 有时候更管用。",
                 "注意指针运算！C 语言指针+1 是加一个类型的大小，不是加 1 字节。",
                 "看寄存器状态，scause 和 sepc 是定位 Trap 的关键。"
               ]} />
               <NoteCard title="心态建设" points={[
                 "环境配置是最难的一步，配好环境就成功了一半。",
                 "不要抄代码，理解原理最重要，答辩会问细节。",
                 "遇到 Bug 睡一觉，第二天往往就有思路了。"
               ]} />
            </div>
         </div>
      </section>

      {/* Footer */}
      <footer className="py-12 bg-[#ECF0F1] border-t border-[#D8DEE9] text-center">
         <div className="flex items-center justify-center gap-2 mb-4 text-[#2E3440] font-bold text-lg">
           <GraduationCap className="text-[#5E81AC]"/> UCAS OS Kernel
         </div>
         <p className="text-[#4C566A] text-sm font-mono">Designed & Built by Stu</p>
      </footer>
    </div>
  );
}
