import XCTest
import SwiftTreeSitter
import TreeSitterMinml

final class TreeSitterMinmlTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_minml())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading MinML grammar")
    }
}
