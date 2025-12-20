import React, { useState, useEffect, useRef } from 'react';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import { Prism as SyntaxHighlighter } from 'react-syntax-highlighter';
import { nord } from 'react-syntax-highlighter/dist/esm/styles/prism'; 
import { Menu, X, ChevronDown, ChevronRight, Home, Github } from 'lucide-react';
import { useParams, useNavigate, Link } from 'react-router-dom';

// Enhanced slugify function to handle more characters including dots and spaces properly
const slugify = (text) => {
  return text
    .toLowerCase()
    .trim()
    .replace(/\s+/g, '-')           // Replace spaces with -
    .replace(/[^\w\u4e00-\u9fa5\-\.]+/g, '') // Remove all non-word chars except Chinese, -, and .
    .replace(/\-\s*-\s*/g, '-')         // Replace multiple - with single -
    .replace(/^-+/, '')             // Trim - from start of text
    .replace(/-+$/, '');            // Trim - from end of text
};

export default function TaskReader() {
  const { noteId } = useParams();
  const navigate = useNavigate();
  const scrollContainerRef = useRef(null);
  
  const [allNotes, setAllNotes] = useState([]); 
  const [content, setContent] = useState('');
  const [toc, setToc] = useState([]); 
  const [loading, setLoading] = useState(true);
  const [sidebarOpen, setSidebarOpen] = useState(true); 
  const [activeHeader, setActiveHeader] = useState('');
  
  // State to track expanded projects in sidebar
  const [expandedProjects, setExpandedProjects] = useState({});

  // 1. Fetch Index
  useEffect(() => {
    fetch('./notes-index.json')
      .then(res => res.json())
      .then(data => {
        setAllNotes(data);
        if (!noteId && data.length > 0) {
            navigate(`/read/${data[0].id}`, { replace: true });
        }
      })
      .catch(err => console.error("Failed to load notes index", err));
  }, []);

  // 2. Fetch Content & Handle Expansion
  useEffect(() => {
    if (!noteId || allNotes.length === 0) return;

    // Auto-expand the current project
    setExpandedProjects(prev => ({
        ...prev,
        [noteId]: true
    }));

    const note = allNotes.find(n => n.id === noteId);
    if (!note) {
        setContent("# 404\nProject not found.");
        setLoading(false);
        return;
    }

    setLoading(true);
    setToc([]); 
    
    const fetchPath = note.path.startsWith('/') ? '.' + note.path : note.path;
    
    fetch(fetchPath)
        .then(res => res.text())
        .then(text => {
            setContent(text);
            generateTOC(text);
            setLoading(false);
            if (scrollContainerRef.current) {
                scrollContainerRef.current.scrollTop = 0;
            }
            if (window.innerWidth < 1024) setSidebarOpen(false);
        })
        .catch(err => {
            console.error(err);
            setLoading(false);
        });
  }, [noteId, allNotes]);

  // 3. Generate TOC
  const generateTOC = (markdown) => {
    const lines = markdown.split('\n');
    const headers = [];
    const idMap = {};

    lines.forEach(line => {
      const match = line.match(/^(#{2,3})\s+(.+)$/);
      if (match) {
        const level = match[1].length; 
        const title = match[2].trim();
        let slug = slugify(title);
        
        if (idMap[slug]) {
            idMap[slug]++;
            slug = `${slug}-${idMap[slug]}`;
        } else {
            idMap[slug] = 0;
        }
        headers.push({ level, title, slug });
      }
    });
    setToc(headers);
  };

  // 4. Scroll Spy
  useEffect(() => {
    if (loading) return;
    
    const observer = new IntersectionObserver((entries) => {
      entries.forEach(entry => {
        if (entry.isIntersecting) {
          setActiveHeader(entry.target.id);
        }
      });
    }, { 
        root: scrollContainerRef.current,
        rootMargin: '-20% 0px -70% 0px' 
    });

    const headings = document.querySelectorAll('h2, h3');
    headings.forEach(h => observer.observe(h));

    return () => observer.disconnect();
  }, [content, loading]);

  // Handle TOC Click
  const handleTocClick = (slug, e) => {
      e.preventDefault();
      const el = document.getElementById(slug);
      
      if (el && scrollContainerRef.current) {
          const containerTop = scrollContainerRef.current.getBoundingClientRect().top;
          const elementTop = el.getBoundingClientRect().top;
          const relativeTop = elementTop - containerTop + scrollContainerRef.current.scrollTop;
          const headerOffset = 20; 

          scrollContainerRef.current.scrollTo({
              top: relativeTop - headerOffset,
              behavior: "smooth"
          });
          
          setActiveHeader(slug);
          if (window.innerWidth < 1024) setSidebarOpen(false);
      } else {
          console.warn(`Element with id ${slug} not found`);
      }
  };

  // Toggle Project Expansion
  const toggleProject = (id, e) => {
      e.preventDefault();
      e.stopPropagation();
      setExpandedProjects(prev => ({
          ...prev,
          [id]: !prev[id]
      }));
  };

  return (
    <div className="flex h-screen bg-[#eceff4] overflow-hidden font-serif text-[#2e3440]">
      
      {sidebarOpen && (
        <div className="fixed inset-0 bg-black/20 z-20 backdrop-blur-sm lg:hidden" onClick={() => setSidebarOpen(false)} />
      )}

      {/* Sidebar */}
      <div className={`
        fixed inset-y-0 left-0 z-30 w-72 bg-[#e5e9f0] border-r border-[#d8dee9] transform transition-transform duration-300 ease-in-out flex flex-col
        lg:relative lg:translate-x-0 
        ${sidebarOpen ? 'translate-x-0' : '-translate-x-full'}
      `}>
          <div className="p-4 border-b border-[#d8dee9] flex items-center justify-between bg-[#e5e9f0] sticky top-0 z-10">
             <Link to="/" className="font-bold text-[#2e3440] flex items-center gap-2 hover:opacity-80 transition-opacity text-lg">
                <Home size={18} /> <span>UCAS OS</span>
             </Link>
             <button onClick={() => setSidebarOpen(false)} className="lg:hidden text-[#4c566a]">
               <X size={20} />
             </button>
          </div>

          <div className="flex-1 overflow-y-auto py-6 px-4 custom-scrollbar">
             <div className="space-y-1">
                 {allNotes.map((note) => {
                     const isActive = note.id === noteId;
                     const isExpanded = expandedProjects[note.id] || isActive; // Always expanded if active

                     return (
                         <div key={note.id} className="mb-2">
                             {/* Project Title Row */}
                             <div className={`flex items-center justify-between w-full px-3 py-2 rounded-lg text-sm font-bold transition-colors font-sans
                                    ${isActive 
                                        ? 'bg-white text-[#5e81ac] shadow-sm border border-[#d8dee9]' 
                                        : 'text-[#4c566a] hover:bg-[#d8dee9]/50 hover:text-[#2e3440]'
                                    }
                                `}
                             >
                                 <Link 
                                    to={`/read/${note.id}`}
                                    className="flex-1 truncate"
                                 >
                                     {note.title}
                                 </Link>
                                 
                                 <button 
                                    onClick={(e) => toggleProject(note.id, e)}
                                    className="p-1 hover:bg-[#d8dee9] rounded ml-2"
                                 >
                                     {isExpanded ? <ChevronDown size={14} className="text-[#5e81ac]"/> : <ChevronRight size={14} className="text-[#4c566a]"/>}
                                 </button>
                             </div>

                             {/* Nested TOC */}
                             {isActive && isExpanded && toc.length > 0 && (
                                 <div className="mt-1 ml-4 border-l border-[#d8dee9] pl-3 space-y-1 animate-in slide-in-from-left-1 duration-200">
                                     {toc.map((item, idx) => (
                                         <a
                                            key={idx}
                                            href={`#${item.slug}`}
                                            onClick={(e) => handleTocClick(item.slug, e)}
                                            className={`block text-xs py-1 leading-relaxed transition-colors font-sans truncate cursor-pointer select-none
                                                ${item.level === 3 ? 'pl-2 text-[#99aab9]' : 'text-[#4c566a]'}
                                                ${activeHeader === item.slug ? 'text-[#5e81ac] font-bold' : 'hover:text-[#2e3440]'}
                                            `}
                                            title={item.title}
                                         >
                                             {item.title}
                                         </a>
                                     ))}
                                 </div>
                             )}
                         </div>
                     );
                 })}
             </div>
          </div>
          
           <div className="p-4 border-t border-[#d8dee9] bg-[#e5e9f0]">
              <a href="https://github.com/suiqingying/ucas-os-kernel" target="_blank" rel="noreferrer" className="flex items-center justify-center gap-2 text-sm text-[#4c566a] hover:text-[#2e3440] transition-colors">
                  <Github size={16} /> <span>Repository</span>
              </a>
          </div>
      </div>

      {/* Main Content Area */}
      <div 
        className="flex-1 h-full overflow-y-auto relative scroll-smooth bg-[#eceff4]" 
        id="scroll-container"
        ref={scrollContainerRef}
      >
          
          {/* Mobile Header */}
          <div className="lg:hidden sticky top-0 bg-[#eceff4]/95 backdrop-blur border-b border-[#d8dee9] px-4 py-3 flex items-center gap-3 z-10">
              <button onClick={() => setSidebarOpen(true)} className="text-[#4c566a] p-1 rounded hover:bg-[#d8dee9]">
                  <Menu size={20} />
              </button>
              <span className="font-bold text-[#2e3440] text-sm truncate">
                  {allNotes.find(n => n.id === noteId)?.title || 'Loading...'}
              </span>
          </div>

          <div className="max-w-4xl mx-auto px-6 py-10 lg:py-16 lg:px-12">
             {loading ? (
                <div className="animate-pulse space-y-6 mt-10">
                    <div className="h-10 bg-[#d8dee9] rounded w-1/2"></div>
                    <div className="h-4 bg-[#d8dee9] rounded w-full"></div>
                    <div className="h-4 bg-[#d8dee9] rounded w-full"></div>
                </div>
             ) : (
                <article className="prose prose-stone prose-lg max-w-none 
                    prose-headings:font-serif prose-headings:font-bold prose-headings:text-[#2e3440]
                    prose-h1:text-4xl prose-h1:mb-10 prose-h1:pb-4 prose-h1:border-b prose-h1:border-[#d8dee9]
                    prose-h2:text-2xl prose-h2:mt-12 prose-h2:mb-4 prose-h2:text-[#5e81ac] prose-h2:flex prose-h2:items-center
                    prose-p:leading-8 prose-p:text-[#4c566a] prose-p:font-serif prose-p:mb-6
                    prose-a:text-[#5e81ac] prose-a:no-underline hover:prose-a:underline
                    prose-pre:bg-[#2e3440] prose-pre:border prose-pre:border-[#4c566a] prose-pre:text-[#eceff4] prose-pre:shadow-sm prose-pre:rounded-xl
                    prose-code:text-[#d08770] prose-code:bg-[#e5e9f0] prose-code:px-1.5 prose-code:py-0.5 prose-code:rounded prose-code:font-mono prose-code:text-[0.85em]
                    prose-blockquote:border-l-4 prose-blockquote:border-[#81a1c1] prose-blockquote:bg-[#e5e9f0]/50 prose-blockquote:py-3 prose-blockquote:px-5 prose-blockquote:italic prose-blockquote:text-[#4c566a] prose-blockquote:rounded-r-lg
                    prose-li:marker:text-[#88c0d0]
                ">
                    <ReactMarkdown 
                        children={content} 
                        remarkPlugins={[remarkGfm]}
                        components={{
                            h1: ({node, ...props}) => <h1 id={slugify(props.children[0])} {...props} />,
                            h2: ({node, ...props}) => <h2 id={slugify(props.children[0])} {...props} />,
                            h3: ({node, ...props}) => <h3 id={slugify(props.children[0])} {...props} />,
                            code({node, inline, className, children, ...props}) {
                                const match = /language-(\w+)/.exec(className || '')
                                return !inline && match ? (
                                    <div className="not-prose my-6 rounded-xl overflow-hidden border border-[#d8dee9] shadow-sm bg-[#2e3440]">
                                        <div className="bg-[#3b4252] px-4 py-1.5 text-xs font-mono font-bold text-[#d8dee9] border-b border-[#4c566a] flex justify-between uppercase tracking-wider">
                                            {match[1]}
                                        </div>
                                        <SyntaxHighlighter
                                            {...props}
                                            children={String(children).replace(/\n$/, '')}
                                            style={nord}
                                            language={match[1]}
                                            customStyle={{
                                                margin: 0,
                                                padding: '1.5rem',
                                                background: 'transparent',
                                                fontSize: '0.9rem',
                                                lineHeight: '1.6',
                                            }}
                                        />
                                    </div>
                                ) : (
                                    <code {...props} className={className}>
                                        {children}
                                    </code>
                                )
                            }
                        }}
                    />
                </article>
             )}
             
             {/* Navigation Footer */}
             {!loading && allNotes.length > 0 && (
                <div className="mt-20 border-t border-[#d8dee9] pt-8 flex flex-col sm:flex-row justify-between gap-4 font-serif">
                   {(() => {
                       const idx = allNotes.findIndex(n => n.id === noteId);
                       const prev = idx > 0 ? allNotes[idx - 1] : null;
                       const next = idx < allNotes.length - 1 ? allNotes[idx + 1] : null;
                       
                       return (
                           <>
                             {prev ? (
                               <Link to={`/read/${prev.id}`} className="group flex flex-col items-start p-4 border border-[#d8dee9] rounded-lg hover:border-[#81a1c1] hover:bg-white transition-all w-full sm:w-1/2">
                                  <span className="text-xs text-[#99aab9] uppercase tracking-widest mb-1 group-hover:text-[#5e81ac]">Previous</span>
                                  <span className="text-lg font-bold text-[#4c566a] group-hover:text-[#2e3440]">{prev.title}</span>
                               </Link>
                             ) : <div className="hidden sm:block w-1/2"/>}
                             
                             {next ? (
                               <Link to={`/read/${next.id}`} className="group flex flex-col items-end p-4 border border-[#d8dee9] rounded-lg hover:border-[#81a1c1] hover:bg-white transition-all w-full sm:w-1/2 text-right">
                                  <span className="text-xs text-[#99aab9] uppercase tracking-widest mb-1 group-hover:text-[#5e81ac]">Next</span>
                                  <span className="text-lg font-bold text-[#4c566a] group-hover:text-[#2e3440]">{next.title}</span>
                               </Link>
                             ) : <div className="hidden sm:block w-1/2"/>}
                           </>
                       );
                   })()}
                </div>
             )}
          </div>
      </div>
    </div>
  );
}
