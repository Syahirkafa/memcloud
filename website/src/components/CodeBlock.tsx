interface CodeBlockProps {
  code: string;
  language?: string;
}

export const CodeBlock = ({ code, language = "typescript" }: CodeBlockProps) => {
  return (
    <div className="bg-terminal-bg border border-terminal-border rounded-xl overflow-hidden">
      <div className="flex items-center justify-between px-4 py-2 border-b border-terminal-border">
        <span className="text-xs text-muted-foreground font-mono">{language}</span>
      </div>
      <pre className="p-6 overflow-x-auto bg-[#1e1e1e]">
        <code className="text-sm font-mono text-[#d4d4d4] leading-relaxed whitespace-pre font-medium">{code}</code>
      </pre>
    </div>
  );
};
