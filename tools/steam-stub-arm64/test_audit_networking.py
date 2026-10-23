import unittest
from audit_networking import exported_symbols, inventory


class AuditTests(unittest.TestCase):
    def test_filter_and_return_abi(self):
        result = inventory("# comment\nsteam_api\tSteamAPI_ISteamNetworkingSockets_ConnectP2P\tuint\tu64\n"
                           "steam_api\tSteamAPI_ISteamUser_GetAuthSessionTicket\tuint\tu64\n"
                           "steam_api\tSteamAPI_ISteamApps_GetAppOwner\tulong\tu64\n"
                           "other\tNetworking\tvoid\tvoid\n")
        self.assertEqual(len(result), 2)
        self.assertEqual(result["SteamAPI_ISteamNetworkingSockets_ConnectP2P"]["abi_return"], "u64")

    def test_versioned_symbols(self):
        self.assertEqual(exported_symbols("00001 T Foo@@V1\n00002 T Bar\n\n"), {"Foo", "Bar"})

    def test_empty(self):
        self.assertEqual(inventory("# only a comment\n"), {})

    def test_malformed_inventory_rejected(self):
        with self.assertRaises(ValueError):
            inventory("steam_api\tMissingFields")


if __name__ == "__main__":
    unittest.main()
