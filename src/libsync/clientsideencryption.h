#ifndef CLIENTSIDEENCRYPTION_H
#define CLIENTSIDEENCRYPTION_H

#include <QString>
#include <QObject>
#include <QJsonDocument>
#include <QSslCertificate>
#include <QSslKey>
#include <QFile>
#include <QVector>
#include <QMap>

#include <openssl/evp.h>

#include "accountfwd.h"
#include "networkjobs.h"

namespace QKeychain {
class Job;
class WritePasswordJob;
class ReadPasswordJob;
}

namespace OCC {

QString e2eeBaseUrl();

namespace EncryptionHelper {
    QByteArray generateRandomFilename();
    OWNCLOUDSYNC_EXPORT QByteArray generateRandom(int size);
    QByteArray generatePassword(const QString &wordlist, const QByteArray& salt);
    OWNCLOUDSYNC_EXPORT QByteArray encryptPrivateKey(
            const QByteArray& key,
            const QByteArray& privateKey,
            const QByteArray &salt
    );
    OWNCLOUDSYNC_EXPORT QByteArray decryptPrivateKey(
            const QByteArray& key,
            const QByteArray& data
    );
    OWNCLOUDSYNC_EXPORT QByteArray extractPrivateKeySalt(const QByteArray &data);
    OWNCLOUDSYNC_EXPORT QByteArray encryptStringSymmetric(
            const QByteArray& key,
            const QByteArray& data
    );
    OWNCLOUDSYNC_EXPORT QByteArray decryptStringSymmetric(
            const QByteArray& key,
            const QByteArray& data
    );
    OWNCLOUDSYNC_EXPORT QByteArray encryptStringAsymmetric(const QSslKey key, const QByteArray &data);
    OWNCLOUDSYNC_EXPORT QByteArray decryptStringAsymmetric(const QByteArray &privateKeyPem, const QByteArray &data);

    QByteArray privateKeyToPem(const QByteArray key);

    //TODO: change those two EVP_PKEY into QSslKey.
    QByteArray encryptStringAsymmetric(
            EVP_PKEY *publicKey,
            const QByteArray& data
    );
    QByteArray decryptStringAsymmetric(
            EVP_PKEY *privateKey,
            const QByteArray& data
    );

    OWNCLOUDSYNC_EXPORT bool fileEncryption(const QByteArray &key, const QByteArray &iv,
                      QFile *input, QFile *output, QByteArray& returnTag);

    OWNCLOUDSYNC_EXPORT bool fileDecryption(const QByteArray &key, const QByteArray &iv,
                               QFile *input, QFile *output);

//
// Simple classes for safe (RAII) handling of OpenSSL
// data structures
//
class CipherCtx {
public:
    CipherCtx() : _ctx(EVP_CIPHER_CTX_new())
    {
    }

    ~CipherCtx()
    {
        EVP_CIPHER_CTX_free(_ctx);
    }

    operator EVP_CIPHER_CTX*()
    {
        return _ctx;
    }

private:
    Q_DISABLE_COPY(CipherCtx)
    EVP_CIPHER_CTX *_ctx;
};

class OWNCLOUDSYNC_EXPORT StreamingDecryptor
{
public:
    StreamingDecryptor(const QByteArray &key, const QByteArray &iv, quint64 totalSize);
    ~StreamingDecryptor() = default;

    QByteArray chunkDecryption(const char *input, quint64 chunkSize);

    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] bool isFinished() const;

private:
    Q_DISABLE_COPY(StreamingDecryptor)

    CipherCtx _ctx;
    bool _isInitialized = false;
    bool _isFinished = false;
    quint64 _decryptedSoFar = 0;
    quint64 _totalSize = 0;
};
}

class OWNCLOUDSYNC_EXPORT ClientSideEncryption : public QObject {
    Q_OBJECT
public:
    class PKey;

    ClientSideEncryption();

    QByteArray _privateKey;
    QSslKey _publicKey;
    QSslCertificate _certificate;
    QString _mnemonic;
    bool _newMnemonicGenerated = false;

signals:
    void initializationFinished(bool isNewMnemonicGenerated = false);
    void sensitiveDataForgotten();
    void privateKeyDeleted();
    void certificateDeleted();
    void mnemonicDeleted();

public slots:
    void initialize(const AccountPtr &account);
    void forgetSensitiveData(const AccountPtr &account);

private slots:
    void generateKeyPair(const AccountPtr &account);
    void encryptPrivateKey(const AccountPtr &account);    

    void publicKeyFetched(QKeychain::Job *incoming);
    void privateKeyFetched(QKeychain::Job *incoming);
    void mnemonicKeyFetched(QKeychain::Job *incoming);

    void handlePrivateKeyDeleted(const QKeychain::Job* const incoming);
    void handleCertificateDeleted(const QKeychain::Job* const incoming);
    void handleMnemonicDeleted(const QKeychain::Job* const incoming);
    void checkAllSensitiveDataDeleted();

    void getPrivateKeyFromServer(const AccountPtr &account);
    void getPublicKeyFromServer(const AccountPtr &account);
    void fetchAndValidatePublicKeyFromServer(const AccountPtr &account);
    void decryptPrivateKey(const AccountPtr &account, const QByteArray &key);

    void fetchFromKeyChain(const AccountPtr &account);
    void writePrivateKey(const AccountPtr &account);
    void writeCertificate(const AccountPtr &account);
    void writeMnemonic(const AccountPtr &account);

private:
    void generateCSR(const AccountPtr &account, PKey keyPair);
    void sendSignRequestCSR(const AccountPtr &account, PKey keyPair, const QByteArray &csrContent);

    [[nodiscard]] bool checkPublicKeyValidity(const AccountPtr &account) const;
    [[nodiscard]] bool checkServerPublicKeyValidity(const QByteArray &serverPublicKeyString) const;

    bool isInitialized = false;
};

/* Generates the Metadata for the folder */
struct EncryptedFile {
    QByteArray encryptionKey;
    QByteArray mimetype;
    QByteArray initializationVector;
    QByteArray authenticationTag;
    QString encryptedFilename;
    QString originalFilename;
    int fileVersion = 0;
    int metadataKey = 0;
};

class OWNCLOUDSYNC_EXPORT FolderMetadata {
public:
    FolderMetadata(AccountPtr account, const QByteArray& metadata = QByteArray(), int statusCode = -1);
    QByteArray encryptedMetadata();
    void addEncryptedFile(const EncryptedFile& f);
    void removeEncryptedFile(const EncryptedFile& f);
    void removeAllEncryptedFiles();
    [[nodiscard]] QVector<EncryptedFile> files() const;
    [[nodiscard]] bool isMetadataSetup() const;


private:
    /* Use std::string and std::vector internally on this class
     * to ease the port to Nlohmann Json API
     */
    void setupEmptyMetadata();
    void setupExistingMetadata(const QByteArray& metadata);

    [[nodiscard]] QByteArray encryptMetadataKey(const QByteArray& metadataKey) const;
    [[nodiscard]] QByteArray decryptMetadataKey(const QByteArray& encryptedKey) const;

    [[nodiscard]] QByteArray encryptJsonObject(const QByteArray& obj, const QByteArray pass) const;
    [[nodiscard]] QByteArray decryptJsonObject(const QByteArray& encryptedJsonBlob, const QByteArray& pass) const;

    QVector<EncryptedFile> _files;
    QMap<int, QByteArray> _metadataKeys;
    AccountPtr _account;
    QVector<QPair<QString, QString>> _sharing;
};

} // namespace OCC
#endif
