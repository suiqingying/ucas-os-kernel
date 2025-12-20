import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const notesDir = path.join(__dirname, 'public', 'notes');
const outputFile = path.join(__dirname, 'public', 'notes-index.json');

try {
  if (!fs.existsSync(notesDir)) {
    console.error('Notes directory not found:', notesDir);
    process.exit(1);
  }

  const files = fs.readdirSync(notesDir)
    .filter(file => file.endsWith('.md'))
    .sort();

  const indexData = files.map(file => {
    const content = fs.readFileSync(path.join(notesDir, file), 'utf-8');
    const firstLine = content.split('\n')[0];
    const title = firstLine.replace(/^#\s*/, '').trim() || file.replace('.md', '');
    
    return {
      id: file.replace('.md', ''),
      fileName: file,
      title: title,
      path: `/notes/${file}`
    };
  });

  fs.writeFileSync(outputFile, JSON.stringify(indexData, null, 2));
  console.log(`Generated index with ${indexData.length} notes at ${outputFile}`);
} catch (err) {
  console.error('Error generating index:', err);
  process.exit(1);
}
