import importlib.machinery
import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).parent / "hammer-migrate-v3.py"
LOADER = importlib.machinery.SourceFileLoader("hammer_migrate_v3", str(SCRIPT))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
MIGRATE = importlib.util.module_from_spec(SPEC)
LOADER.exec_module(MIGRATE)


class HammerMigrateV3Test(unittest.TestCase):
    def test_migrates_public_union_members(self):
        old = (
            "HParsedToken *token; HCaseResult result;\n"
            "x = token->uint; y = token->bytes.len; z = result.parse_time;\n"
        )
        new, counts = MIGRATE.migrate_source(old)
        self.assertEqual(
            new,
            "HParsedToken *token; HCaseResult result;\n"
            "x = token->token_data.uint; y = token->token_data.bytes.len; "
            "z = result.timestamp.parse_time;\n",
        )
        self.assertEqual(counts, {"token_data": 2, "timestamp": 1})

    def test_is_idempotent(self):
        source = (
            "HParsedToken *token; HCaseResult result;\n"
            "x = token->token_data.uint; y = result.timestamp.parse_time;\n"
        )
        once, _ = MIGRATE.migrate_source(source)
        twice, _ = MIGRATE.migrate_source(once)
        self.assertEqual(once, source)
        self.assertEqual(twice, source)

    def test_ignores_comments_and_literals(self):
        source = '/* token->uint */\n// token->sint\nprintf("token->bytes");\n'
        migrated, counts = MIGRATE.migrate_source(source)
        self.assertEqual(migrated, source)
        self.assertEqual(counts, {"token_data": 0, "timestamp": 0})

    def test_does_not_change_unrelated_member_names(self):
        source = "HCFChoice *choice; x = choice->data.seq; y = unrelated->uint;\n"
        migrated, counts = MIGRATE.migrate_source(source)
        self.assertEqual(migrated, source)
        self.assertEqual(counts, {"token_data": 0, "timestamp": 0})

    def test_recognizes_parse_result_ast(self):
        source = "HParseResult *result; x = result->ast->sint;\n"
        migrated, _ = MIGRATE.migrate_source(source)
        self.assertEqual(migrated, "HParseResult *result; x = result->ast->token_data.sint;\n")

    def test_does_not_assume_every_ast_is_a_hammer_token(self):
        source = "struct Other *ast; x = ast->uint;\n"
        migrated, _ = MIGRATE.migrate_source(source)
        self.assertEqual(migrated, source)

    def test_handles_multiple_and_array_declarators(self):
        source = (
            "HParsedToken *first, *second, tokens[2];\n"
            "x = second->uint; y = tokens[0].sint;\n"
        )
        migrated, counts = MIGRATE.migrate_source(source)
        self.assertEqual(
            migrated,
            "HParsedToken *first, *second, tokens[2];\n"
            "x = second->token_data.uint; y = tokens[0].token_data.sint;\n",
        )
        self.assertEqual(counts["token_data"], 2)

    def test_preserves_crlf(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.c"
            path.write_bytes(b"HParsedToken *token;\r\nx = token->uint;\r\n")
            self.assertEqual(MIGRATE.main(["--write", str(path)]), 0)
            self.assertEqual(
                path.read_bytes(),
                b"HParsedToken *token;\r\nx = token->token_data.uint;\r\n",
            )


if __name__ == "__main__":
    unittest.main()
