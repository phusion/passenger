require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'utils/strscan'

module PhusionPassenger

describe Utils::StringScanner do
  def new_scanner(data)
    Utils::StringScanner.new(data)
  end

  specify '#pos is initially 0' do
    scanner = new_scanner('ab')
    expect(scanner.pos).to eq(0)
  end

  specify '#matched is initially nil' do
    scanner = new_scanner('ab')
    expect(scanner.matched).to be_nil
  end

  describe '#getch' do
    it 'gets the next character and increments pos' do
      scanner = new_scanner('ab')
      expect(scanner.getch).to eq('a')
      expect(scanner.matched).to eq('a')
      expect(scanner.pos).to eq(1)
      expect(scanner.getch).to eq('b')
      expect(scanner.matched).to eq('b')
      expect(scanner.pos).to eq(2)
    end

    specify 'when data is empty, it returns nil and does not increment pos' do
      scanner = new_scanner('')
      expect(scanner.getch).to be_nil
      expect(scanner.matched).to be_nil
      expect(scanner.pos).to eq(0)
    end

    specify 'upon reaching EOS, it returns nil and does not increment pos' do
      scanner = new_scanner('a')
      scanner.getch
      expect(scanner.getch).to be_nil
      expect(scanner.matched).to be_nil
      expect(scanner.pos).to eq(1)
    end
  end

  describe '#scan' do
    describe 'if the pattern matches at the current pos' do
      it 'returns the match and increments pos' do
        scanner = new_scanner('aaaax')
        expect(scanner.scan(/a+/)).to eq('aaaa')
        expect(scanner.matched).to eq('aaaa')
        expect(scanner.pos).to eq(4)
        expect(scanner.scan(/x+/)).to eq('x')
        expect(scanner.matched).to eq('x')
        expect(scanner.pos).to eq(5)
      end
    end

    describe 'if the pattern does not match at the current pos' do
      it 'returns nil and does not increment pos' do
        scanner = new_scanner('aaaax')
        expect(scanner.scan(/x+/)).to be_nil
        expect(scanner.matched).to be_nil
        expect(scanner.pos).to eq(0)

        scanner.scan(/a+/)

        expect(scanner.scan(/y+/)).to be_nil
        expect(scanner.matched).to be_nil
        expect(scanner.pos).to eq(4)
      end
    end

    it 'scans across newlines' do
      scanner = new_scanner("a\na")
      expect(scanner.scan(/[a\s]+/)).to eq("a\na")
      expect(scanner.matched).to eq("a\na")
      expect(scanner.pos).to eq(3)
    end
  end
end

end
