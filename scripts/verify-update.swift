// Checks an update's EdDSA signature against the public key in the app's Info.plist, as Sparkle will.
// Usage: swift scripts/verify-update.swift MF-1.app UPDATE.dmg SIGNATURE
import CryptoKit
import Foundation

let args = CommandLine.arguments
guard args.count == 4,
      let info = NSDictionary(contentsOfFile: args[1] + "/Contents/Info.plist"),
      let key = (info["SUPublicEDKey"] as? String).flatMap({ Data(base64Encoded: $0) }),
      let sig = Data(base64Encoded: args[3]),
      let data = FileManager.default.contents(atPath: args[2]) else {
    fputs("usage: verify-update.swift MF-1.app UPDATE SIGNATURE\n", stderr)
    exit(2)
}
let ok = (try? Curve25519.Signing.PublicKey(rawRepresentation: key))?.isValidSignature(sig, for: data) ?? false
print(ok ? "signature matches the app's key" : "signature does NOT match the app's key")
exit(ok ? 0 : 1)
