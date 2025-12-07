class Memcloud < Formula
  desc "Distributed RAM Cloud"
  homepage "https://github.com/vibhanshu2001/memcloud"
  license "MIT"

  if OS.mac?
    if Hardware::CPU.intel?
      url "https://github.com/vibhanshu2001/memcloud/releases/download/v0.1.0/memcloud-x86_64-apple-darwin.tar.gz"
      sha256 "0000000000000000000000000000000000000000000000000000000000000000"
    elsif Hardware::CPU.arm?
      url "https://github.com/vibhanshu2001/memcloud/releases/download/v0.1.0/memcloud-aarch64-apple-darwin.tar.gz"
      sha256 "0000000000000000000000000000000000000000000000000000000000000000"
    end
  elsif OS.linux?
    url "https://github.com/vibhanshu2001/memcloud/releases/download/v0.1.0/memcloud-x86_64-unknown-linux-gnu.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end

  def install
    bin.install "memnode"
    bin.install "memcli"
  end

  plist_options manual: "memnode"

  def plist
    <<~EOS
      <?xml version="1.0" encoding="UTF-8"?>
      <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
      <plist version="1.0">
      <dict>
        <key>Label</key>
        <string>#{plist_name}</string>
        <key>ProgramArguments</key>
        <array>
          <string>#{opt_bin}/memnode</string>
        </array>
        <key>KeepAlive</key>
        <true/>
        <key>RunAtLoad</key>
        <true/>
        <key>StandardOutPath</key>
        <string>#{var}/log/memcloud.log</string>
        <key>StandardErrorPath</key>
        <string>#{var}/log/memcloud.err</string>
      </dict>
      </plist>
    EOS
  end

  test do
    system "#{bin}/memnode", "--version"
  end
end
