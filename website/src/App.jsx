import React from 'react';
import { HashRouter as Router, Routes, Route } from 'react-router-dom';
import Home from './components/Home';
import TaskReader from './components/TaskReader';

export default function App() {
  return (
    <Router>
      <Routes>
        <Route path="/" element={<Home />} />
        <Route path="/read/:noteId" element={<TaskReader />} />
        <Route path="/read" element={<TaskReader />} />
      </Routes>
    </Router>
  );
}