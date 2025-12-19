import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react()],
  base: '/ucas-os-kernel/', // GitHub 仓库名，非常重要！
  build: {
    outDir: '../docs',      // 编译输出到根目录的 docs 文件夹
    emptyOutDir: true,      // 编译前清空 docs 目录
  },
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
})